
#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>

/**
 * 1. threads
 *     Seastar-based programs run a single thread on each CPU , Each of these threads runs its own event loop called `engine`
 *     Seastar application will take over all the available cores, starting one thread per core
 *
 *  2. memory
 *     Seastar applications `shard` their memory. Each thread is preallocated with a large piece of memory
 *     -m option : sepecify the amount of memory given to the Seastar application
 *
 * 3. futures
 *    using futures ,building blocks of asynchronous programming and composing them together into a large is easy
 *    A `future` is a result of a computation that may not be available yet
 *    type future<int> variable holds an int that will eventually be available;  future<> indicates something which will eventually complete, but not return any value.;related methods: available(), get(),
 *    A future is usually returned by an `asynchronous function`,Because asynchrnous functions `promise` to eventually resolve the future,asynchronous functions are sometimes called "promises"
 *    future is single use: if we have a future<int> variable, as soon as we get() or then() it, it becomes invalid
 *
 * 4. continuation
 *    A continuation is a callback (typically a lambda) to run when a future becomes available
 *    A continuation is attached to a future with the then() method; The return value of then() is itself a future
 *    continuations on the same thread do not run in parallel, so do not need locks, atomic variables
 *    continuations obviously must not use blocking operations, or they block the whole thread.
 *
 * 5. execption
 *    An exception thrown in a continuation is implicitly captured by the system and stored in the future;
 *    Calling .then() on such a future(store execption info) will skips  the continuation, and transfers the exception from the input future to the output future (.then()'s return value).  (aborting the current chain of operations)
 *    fine-grained execption control
 *            .finally() continuation
 *                   executed whether or not its input future carries an exception or not
 *                   The result of the finally continuation is its input future,
 *                   used to insert code in a flow that is executed unconditionally, meanwhile, does not alter the flow
 *            .then_wrapped() continuation
 *                   passes the input future to the continuation,this the continuation can examine whether it contains a value or an exception, and take appropriate action
 *            .futurize_invoke(func, args...): runs a function which may return either a future value or an immediate value, and in both cases convert the result into a future (into a failed future if the func throw exception)
 *
 */
void test1(int argc,char** argv){
    seastar::app_template app;
    try {
        app.run(argc, argv,[]{ //the app's initialization lambda function is run only on one thread  (only print once )
            std::cout<<seastar::smp::count<<std::endl; // number of started threads
            return seastar::make_ready_future();
        });
    }catch(...){
        std::cerr << "Failed to start application: "
                  << std::current_exception() << "\n";
    }
}

#include <seastar/core/sleep.hh>
void test2(int argc,char** argv){
    seastar::app_template app;
    app.run(argc,argv,[]{
        std::cout << "Sleeping... " << std::endl;
        using namespace std::chrono_literals;
        return seastar::sleep(10s).then([] {
            std::cout << "Done.\n";
        });
    });
}

extern seastar::future<int> f();
seastar::future<int> f(){
    std::cout << "Sleeping... " << std::endl;
    using namespace std::chrono_literals;
    return seastar::sleep(10s).then([] {
        std::cout << "Done.\n";
        int count=1;
        return seastar::sleep(5s).then([count](){  // use lambda to capture state, which is used in the continuation execution  (a `copy` of the value of count was saved into the continuation)
            //Using capture-by-reference in a continuation is usually a mistake
            std::cout << "Done.\n";
            return count+1;
        }).then([](int val){// val catch the previous future val
            std::cout << "previous future val:"<<val<<std::endl;
            return val+1;
        });
    });
}

// Using capture-by-reference in a continuation is usually a mistake
// do_with() idiom can use capture-by-reference safely
seastar::future<int> incr() {
    using namespace std::chrono_literals;
    int i=1;
    return seastar::sleep(10ms).then([&i] { return i + 1; });//when the continuation eventually gets to run, the address of `i`  may contain unrelated content
}

// capture-by-move :transfer ownership of object to the continuation
template <typename T>
int do_something(std::unique_ptr<T> obj) {
    return 17;
}
template <typename T>
seastar::future<int> slow_do_something(std::unique_ptr<T> obj) {
    using namespace std::chrono_literals;
//    return seastar::sleep(10ms).then([obj] () mutable { return do_something(std::move(obj)); }); // unique_ptr do not have copy constructor
    return seastar::sleep(10ms).then([obj = std::move(obj)] () mutable {
        return do_something(std::move(obj));
    });
}

//start several sleep() futures in parallel, and attach a different continuation to each
//seastar::future<int> f() {
//    std::cout << "Sleeping... " << std::flush;
//    using namespace std::chrono_literals;
//    seastar::sleep(3s).then([] { std::cout << "3s " << std::flush; });
//    seastar::sleep(1s).then([] { std::cout << "1s " << std::flush; });
//    return seastar::sleep(5s).then([] {
//        std::cout << "Done.\n";
//        return 1;
//    });
//}


// Evaluation order considerations:
template <typename T>
seastar::future<int> test4(std::unique_ptr<T> obj) {
    //  C++14  does not guarantee that lambda captures in continuations will be evaluated after the futures (generated by do_something)
//    return do_something(obj).then([obj = std::move(obj)] () mutable {  // [obj = std::move(obj)] might be evaluated before do_something(obj) is called, potentially leading to `use-after-move` problem
//        return do_something_else(std::move(obj));
//    });
    //  broken into separate statments to guarantee the desired evaluation order
    auto fut = do_something(obj);
    return fut.then([obj = std::move(obj)] () mutable {
        return do_something_else(std::move(obj));
    });
}


//execption
#include <exception>
class my_exception : public std::exception {
    virtual const char* what() const noexcept override { return "my exception"; }
};
seastar::future<> fail() {
    // we can also use make_exception_future_with_backbrace  , which will also print some address info, which can be used to locate this execption
    return seastar::make_exception_future<>(my_exception());  // next  .finally() will  execute
}
seastar::future<> fail2() {
    throw my_exception();  // next  .finally() will  not  execute ;
}
seastar::future<> test5() {
    return fail().finally([] {
        std::cout << "cleaning up\n";
    });
}

//asynchronous functions should always return a (failed)future rather than throw an actual exception ,in some fatal error(out of memory) ,we still need throwing exception
void inner() {
    throw my_exception();
}
seastar::future<> fail3() {
    try {
        inner();
    } catch(...) {
        return seastar::make_exception_future(std::current_exception());
    }
    return seastar::make_ready_future<>();
}

// futurize_invoke(func, args...) :runs a function which may return either a future value or an immediate value,
//                                and in both cases convert the result into a future ( a failed future if the func throw exception)
seastar::future<> test6() {
    return seastar::futurize_invoke(inner).finally([] {
        std::cout << "cleaning up\n";
    });
}

// when thow execption in continuation , will not affect the next continuations
seastar::future<> test7() {
    using namespace std::chrono_literals;
    return seastar::sleep(1s).then([] {
        throw my_exception();  //  unlike asynchronous function throw exceptions , the finally block will execute properly
    }).finally([] {
        std::cout << "cleaning up\n";
    });
}
int start(int argc,char** argv){
    seastar::app_template app;
    try {
//        app.run(argc, argv, f);
//        app.run(argc, argv, incr);
//        app.run(argc, argv, test7);
//        app.run(argc, argv, test6);
//        app.run(argc, argv, test4);
        std::cout<<"app run done"<<std::endl;
    } catch(...) {
        std::cerr << "Couldn't start application: "
                  << std::current_exception() << "\n";
        return 1;
    }
    return 0;
}

int main(int argc,char** argv) {
//    test1(argc,argv);
//    test2(argc,argv);
    return start(argc,argv);

}


