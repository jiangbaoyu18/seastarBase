
#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>
#include <algorithm>
#include <boost/iterator/counting_iterator.hpp>
#include <seastar/core/future-util.hh>
#include <seastar/net/api.hh>
#include <seastar/core/thread.hh>


/**
 *  network stack:
 *        Seastar's network stack is `sharded` just like Seastar applications are, each shard (thread) takes responsibility for a different subset of the connections
 *        Each incoming connection is directed to one of the threads, and after a connection is established, it continues to be handled on the same thread.
 *        seastar::smp::submit_to
 *              smp::submit_to function, we can  easily run service on all cores (shards )
 *
 *  Sharded services
 *       seastar::sharded<T>:  It creates a separate object(can maintain some status) of type T in each core, and provides mechanisms to interact with those objects
 *
 *  Per-thread memory allocation
 *       Seastar takes over a given amount of memory (-m options ) and divides it equally between the different threads
 *       Seastar's different threads can see memory allocated by other threads  (strongly discouraged)
 *       An object allocated on one thread will be owned by this thread, and eventually should be freed by the same thread.
 *       Foreign pointers
 *              if want to reassign ownership of an object to a different thread , using seastar::foreign_ptr<>  (transferring ownership of an object to a another shard)
 *              The most common use-case is a seastar::foreign_ptr<std::unique_ptr<T>>, The thread receiving this foreign_ptr will get exclusive use of the object
 *              when the `receiving thread` destroys this wrapper, it will go back to the `original thread` to destroy the object automatically (the object's destructor is called in `original thread`)
 *              but if we want to execute object's other method (eg: a method need to access other data owned by the object's home shard), we must still be run, explicitly, this method on the home thread: like below (using `submit_to` function)
 *                        * // fp is some foreign_ptr<>
 *                      * return smp::submit_to(fp.get_owner_shard(), [p=fp.get()]
 *                       *  { return p->some_method(); });
 *              Normally, a seastar::foreign_ptr cannot not be copied - only moved; However, when it holds a smart pointer that can be copied  (eg: shared_ptr), we can all foreign_ptr's copy() function to make a copy (copy operation is inefficient and asynchronous)
 *
 *  seastar::thread
 *       A seastar::thread provides an execution environment where `blocking is tolerated`;
 *       You can issue an asyncrhonous function, and wait for it in the same function, rather then establishing a callback to be called with future<>::then():
 *       seastar::thread is not a separate operating system thread. It still uses continuations, which are scheduled on Seastar's single thread.  seastar::threads always run on the same core they were launched on
 *       seastar::thread are never preempted except when seastar::future::get() blocks or on explict calls to seastar::thread::yield()
 *       seastar::thread can only block on Seastar futures, blocking on system calls is not allowed
 *       seastar::future::get()  will throw an exception if the future resolves with an exception,
 *       seastar::future::wait() to wait without fetching the future's result, this can sometimes be useful when you want to avoid throwing an exceptio but wait the future resolved
 *       seastar::async() function provides a convenient shortcut for creating a seastar::thread and returning a future which resolves when the thread completes
 *       seastar::async() and seastar::thread() will have performance penalty(eg: requires additional memory for its stack )  , and will not suitable when execute high concurrent functions
 *
 *  Isolation of application components
 *       throughput and latency of one component should not depend on decisions that another component makes
 *       Scheduling groups (CPU scheduler)
 *
 *
 */


//tcp server repeatedly accepts connections on TCP port 1234, and returns an empty response
const char *canned_response = "Seastar is the future!\n";

seastar::future<> service_loop() {
    seastar::listen_options lo;
    lo.reuse_address = true;// recommended
    return seastar::do_with(seastar::listen(seastar::make_ipv4_address({1234}), lo),
                            [](auto &listener) {
                                return seastar::keep_doing(
                                        [&listener]() {// keep_doing() runs its lambda parameter over and over, starting the next iteration as soon as the future returned by the previous iteration completes; The iterations only stop if an exception is encountered
                                            return listener.accept().then(// server_socket.accept() function returns a future<accept_result>,which contains incoming TCP connection infos
                                                    [](seastar::accept_result res) {
                                                        std::cout << "Accepted connection from " << res.remote_address
                                                                  << "  shard_id:" << seastar::this_shard_id() << "\n";
                                                        seastar::connected_socket conn = std::move(res.connection);
                                                        seastar::output_stream out = conn.output();
                                                        return seastar::do_with(std::move(conn), std::move(out),
                                                                                [](auto &conn, auto &out) {
                                                                                    return out.write(
                                                                                            canned_response).then(
                                                                                            [&out] {// write() operation is a complex asynchronous operation
                                                                                                return out.close();
                                                                                            });
                                                                                });

                                                    });
                                        });
                            });
}

seastar::future<> lauch() {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count

                                      ),
                                      [](unsigned c) {
                                          return seastar::smp::submit_to(c,
                                                                         service_loop);// ask each of Seastar cores (from 0 to smp::count-1) to run the same function service_loop()
                                      });
}

seastar::future<> handle_connection(seastar::connected_socket s,
                                    seastar::socket_address a) {
    auto out = s.output();
    auto in = s.input();
    return do_with(std::move(s), std::move(out), std::move(in),
                   [](auto &s, auto &out, auto &in) {
                       return seastar::repeat([&out, &in] {
                           return in.read().then([&out](seastar::temporary_buffer<char> buf) {
                               if (buf) {
                                   std::cout<<"client data: "<<buf.get()<<std::endl;
                                   return out.write(std::move(buf)).then([&out] {
                                       return out.flush();
                                   }).then([] {
                                       return seastar::stop_iteration::no;
                                   });
                               } else {
                                   return seastar::make_ready_future<seastar::stop_iteration>(
                                           seastar::stop_iteration::yes);
                               }
                           });
                       }).then([&out] {
                           return out.close();
                       });
                   });
}

seastar::future<> service_loop_2() {
    seastar::listen_options lo;
    lo.reuse_address = true;
    return seastar::do_with(seastar::listen(seastar::make_ipv4_address({1234}), lo),
                            [](auto &listener) {
                                return seastar::keep_doing([&listener]() {
                                    return listener.accept().then(
                                            [](seastar::accept_result res) {
                                                //  we do not wait for one  connection to be handled before accepting the next one. (asynchronous function `handle_connection` is in a continuation)
                                                (void) handle_connection(std::move(res.connection), std::move(
                                                        res.remote_address)).handle_exception(
                                                        [](std::exception_ptr ep) {
                                                            std::cout<< "Could not handle connection: "<<ep<<std::endl;
                                                        });
                                            });
                                });
                            });
}

seastar::future<> lauch2() {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
                                      [](unsigned c) {
                                          return seastar::smp::submit_to(c,
                                                                         service_loop_2);// ask each of Seastar cores (from 0 to smp::count-1) to run the same function service_loop()
                                      });
}

seastar::future<> test1() {
//    return lauch();
    return lauch2();
}


//sharded service
class my_service {
public:
    std::string _str;
    my_service(const std::string& str) : _str(str) { }
    seastar::future<> run() {
        std::cerr << "running on " << seastar::engine().cpu_id() <<
                  ", _str = " << _str << "\n";
        return seastar::make_ready_future<>();
    }
    seastar::future<> stop() {  // this method is mandatory
        return seastar::make_ready_future<>();
    }
};

#include <seastar/core/sharded.hh>
seastar::sharded<my_service> s;
seastar::future<> f() {
    return s.start(std::string("hello")).then([] { // s.start() starts the service by creating a my_service object on each of the cores;The arguments to s.start() are passed to my_service's constructor
        // using invoke_on(cpuid, func), we can invoke a service'code , which is in another specific core
        return s.invoke_on_all([] (my_service& local_service) {// s.invoke_on_all() which runs the given lambda on all the cores,  the lambda is given an local service object reference
            return local_service.run();
        });
    }).then([] {
        return s.stop();  // this will call my_service's  stop() to clean up , is mandatory before destroy a service object
    });
}
seastar::future<> test2(){
    return f();
}


// seastar::thread , do not define seastar::thread  var in global
//seastar::thread th([] {
//    std::cout << "Hi.\n";
//    for (int i = 1; i < 4; i++) {
//        seastar::sleep(std::chrono::seconds(1)).get(); // sleep() system call is invalid
//        std::cout << i << "\n";
//    }
//});

//seastar::future<int> get_char();
//seastar::thread th2([]{
//    seastar::future<int> fut = get_char();
//    fut.wait();// wait the future resolved and avoid throwing an exception
//    if (fut.failed()) {
//        return -1;
//    } else {
//        return fut.get0();// Here we already know that get() will return immediately and will not throw.
//    }
//});

//A complete example using seastar::thread
seastar::future<> f2() {
    seastar::thread th([] {
        std::cout << "Hi.\n";
        for (int i = 1; i < 4; i++) {
            seastar::sleep(std::chrono::seconds(1)).get();
            std::cout << i << "\n";
        }
    });
    return do_with(std::move(th), [] (auto& th) {//  keep that seastar::thread object alive until join() completes
        return th.join(); // wait until it ends
    });
}

//seastar::async()
#include <seastar/core/file.hh>
seastar::future<seastar::sstring > read_file(seastar::sstring file_name) {
    return seastar::async([file_name]{ // execute in thread context
        seastar::file f = seastar::open_file_dma(file_name,seastar::open_flags::ro).get0(); // block
        auto buf = f.dma_read_exactly<char>(0, 512).get0();  // "block" again
        return seastar::sstring(buf.get(), buf.size());
    });
}

// test scheduling groups
seastar::future<long> loop(int parallelism, bool& stop) {
    return seastar::do_with(0L, [parallelism, &stop] (long& counter) {
        return seastar::parallel_for_each(boost::irange<unsigned>(0, parallelism),
                                          [&stop, &counter]  (unsigned c) {
                                              return seastar::do_until([&stop] { return stop; }, [&counter] {
                                                  ++counter;
                                                  return seastar::make_ready_future<>();
                                              });
                                          }).then([&counter] { return counter; });
    });
}
seastar::future<> f3() {
    return seastar::do_with(false, [] (bool& stop) {
        seastar::sleep(std::chrono::seconds(2)).then([&stop] {
            stop = true;
        });
        // 1:10
        // By default, Seastar's scheduler keeps a single list of ready-to-run continuations (in each shard)
        // and runs the continuations at the same order they became ready to run
        return seastar::when_all_succeed(loop(1, stop),loop(10,stop)).then(
                [] (long n1,long n2) {
                    std::cout << "Counters: " << n1 << ", " << n2<<"\n";
                });
    });
}

//Seastar allows an application to define separate components known as `scheduling groups`, which each has a separate list of ready-to-run continuations.
// Each scheduling group gets to run its own continuations on a desired percentage of the CPU time
//the number of runnable continuations in one scheduling group `does not affect` the amount of CPU time that another scheduling group gets
seastar::future<long>loop_in_sg(int parallelism, bool& stop, seastar::scheduling_group sg) {
    return seastar::with_scheduling_group(sg, [parallelism, &stop] {
        return loop(parallelism, stop);
    });
}
seastar::future<> f31() {
    return seastar::when_all_succeed(
            // Unlike most objects in Seastar which are separate per shard, Seastar wants the identities and numbering of the scheduling groups to be the same on all shards, so ,the seastar::create_scheduling_group(), is an asynchronous function
            seastar::create_scheduling_group("loop1", 200),  // those two scheduling_group get equal CPU time.
            seastar::create_scheduling_group("loop2", 100)).then(
            [] (seastar::scheduling_group sg1, seastar::scheduling_group sg2) {
                return seastar::do_with(false, [sg1, sg2] (bool& stop) {
                    seastar::sleep(std::chrono::seconds(10)).then([&stop] {
                        stop = true;
                    });
                    return seastar::when_all_succeed(loop_in_sg(1, stop, sg1), loop_in_sg(10, stop, sg2)).then(
                            [] (long n1, long n2) {
                                std::cout << "Counters: " << n1 << ", " << n2 << "\n";
                            });
                });
            });
}

seastar::future<> test3(){
//    return f3();
    return f31();
}


int start(int argc, char **argv) {
    seastar::app_template app;
    try {
//        app.run(argc, argv, test1);
//        app.run(argc, argv, test2);
        app.run(argc, argv, test3);
        std::cout << "app run done" << std::endl;
    } catch (...) {
        std::cerr << "Couldn't start application: "
                  << std::current_exception() << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    return start(argc, argv);
}


