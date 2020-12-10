
#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>
#include <algorithm>
#include <boost/iterator/counting_iterator.hpp>
#include <seastar/core/gate.hh>

/**
 *  Loop :
 *      each iteration in Seastar loop primitives is followed by a `preemption point`,thus allowing other tasks to run inbetween iterations
 *      1. repeat
 *          The loop body passed to repeat is expected to have a future<stop_iteration> return type, which informs if the iteration should continue or stop
 *      2. do_until
 *      3. future seastar::do_for_each(Container& c,AsyncAction action )
 *         future seastar::do_for_each	(Iterator begin,Iterator end,AsyncAction action)
 *      4. parallel_for_each:   high concurrency variant of do_for_each, all iterations are queued simultaneously - which means that there's no guarantee in which order they finish their operations
 *      5. max_concurrent_for_each: a variant of parallel_for_each with restricted parallelism
 *  when_all
 *      Waiting for multiple futures, when_all() accept only rvalues
 *      The future returned by when_all() resolves to a tuple of futures which are already resolved,
 *  when_all_succeed:
 *       if all the given futures have succeeded, it passes the resulting values to continuation, without wrapping them in futures or a tuple
 *       If one or more of the futures failed, when_all_succeed() resolves to a failed future, containing the exception from `one of the failed futures`(randomly choosen)
 *  Semaphores
 *       using for limiting parallelism,resource access
 *       using limiting parallelism of loops
 *
 *  pipe: Seastar's pipe<T> is a mechanism to transfer data between two fibers, one producing data, and the other consuming it
 *  gate
 *      A gate  maintains an internal counter of operations in progress
 *      We call g.enter() when entering an operation ; call g.leave() when leaving the operation
 *      g.close() closes the gate,  it forbids any further calls to g.enter(); g.close() returns a future which resolves when all the existing operations have completed
 *      An operation can check whether its gate was closed by calling the gate's check()
 */

seastar::future<int> recompute_number(int number){
    return seastar::make_ready_future<int>(number*2);
}

//repeat(loop_body)
seastar::future<> push_until_2(seastar::lw_shared_ptr<std::vector<int>> queue, int element) {
    return seastar::repeat([queue, element] {
        if (queue->size() == 2) {
            return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
        }
        return recompute_number(element).then([queue] (int new_element) {
            queue->push_back(new_element);
            return seastar::stop_iteration::no;
        });
    }).then([queue]{
        std::for_each(queue->begin(),queue->end(),[](int x){
            std::cout<<x<<std::endl;  // 6666...
        });
    });
}
seastar::future<> test1() {
    return push_until_2(seastar::make_lw_shared<std::vector<int>>(),3);
}

//do_until(condition,loop_body)
seastar::future<> push_until_3(seastar::lw_shared_ptr<std::vector<int>> queue, int element) {
    return seastar::do_until([queue] { return queue->size() == 3; }, [queue, element] {
        return recompute_number(element).then([queue] (int new_element) {
            queue->push_back(new_element);
        });
    }).then([queue]{
        std::for_each(queue->begin(),queue->end(),[](int x){
            std::cout<<x<<std::endl;  // 6666...
        });
    });
}
seastar::future<> test2() {
    return push_until_3(seastar::make_lw_shared<std::vector<int>>(),3);
}


//do_for_each accepts either an lvalue reference to a container or a pair of iterators  , and the loop_body
// responsibility to ensure that the container is alive during the whole loop execution belongs to the caller
seastar::future<int> do_something(int number){
    using namespace std::chrono_literals;
    return seastar::sleep(1s).then([number]{
        return number*3;
    });
}
/****ERROR examples: there are some chance trigger error  ***********/
seastar::future<> do_for_all_err() {
    std::vector<int> numbers;
    for (int j = 0; j <1000 ; ++j) {
        numbers.push_back(j);
    }
    // Note that the "numbers" vector will be destroyed as soon as this function
    // returns, so we use do_with to guarantee it lives during the whole loop execution:
    return seastar::do_for_each(numbers, [] (int number) {
        return do_something(number).then([](int new_number){
            std::cout<<new_number<<std::endl;
        });
    });
}

//right
seastar::future<> do_for_all() {
    std::vector<int> numbers;
    for (int j = 0; j <1000 ; ++j) {
        numbers.push_back(j);
    }
    // Note that the "numbers" vector will be destroyed as soon as this function
    // returns, so we use do_with to guarantee it lives during the whole loop execution:
    return seastar::do_with(std::move(numbers), [] (std::vector<int>& numbers) {
        return seastar::do_for_each(numbers, [] (int number) {
            return do_something(number).then([](int new_number){ // do_something() is `promise`
                std::cout<<new_number<<std::endl;
            });
        });
    });
}

//parallel_for_each
seastar::future<> do_for_all3() {
    std::vector<int> numbers;
    for (int j = 0; j <1000 ; ++j) {
        numbers.push_back(j);
    }
    // Note that the "numbers" vector will be destroyed as soon as this function
    // returns, so we use do_with to guarantee it lives during the whole loop execution:
    return seastar::do_with(std::move(numbers), [] (std::vector<int>& numbers) {
        return seastar::parallel_for_each(numbers, [] (int number) {
            return do_something(number).then([](int new_number){
                std::cout<<new_number<<std::endl;
            });
        });
    });
}
//seastar::future<> do_for_all4() {
//    std::vector<int> numbers;
//    for (int j = 0; j <1000 ; ++j) {
//        numbers.push_back(j);
//    }
//    // Note that the "numbers" vector will be destroyed as soon as this function
//    // returns, so we use do_with to guarantee it lives during the whole loop execution:
//    return seastar::do_with(std::move(numbers), [] (std::vector<int>& numbers) {
//        return seastar::max_concurrent_for_each(numbers,2, [] (int number) {
//            return do_something(number).then([](int new_number){
//                std::cout<<new_number<<std::endl;
//            });
//        });
//    });
//}

seastar::future<> test3() {
    seastar::future<> f=do_for_all_err();
//    seastar::future<> f=do_for_all();
//    seastar::future<> f=do_for_all3();
//    seastar::future<> f=do_for_all4();
    std::cout<<"function return "<<std::endl;
    std::vector<int> extra_memeory;
    for (int j = 0; j <10000000 ; ++j) {
        extra_memeory.push_back(j);
    }
    return f;
}

//when all
seastar::future<> f() {
    using namespace std::chrono_literals;
    seastar::future<int> slow_two = seastar::sleep(2s).then([] { return 2; });
    return when_all(seastar::sleep(1s), std::move(slow_two),
                    seastar::make_ready_future<double>(3.5)
    ).discard_result();
}
seastar::future<> f2() {
    using namespace std::chrono_literals;
    seastar::future<int> slow_two = seastar::sleep(2s).then([] { return 2; });
    return seastar::when_all(seastar::sleep(1s), std::move(slow_two),
                    seastar::make_ready_future<double>(3.5)
    ).then([](auto tup){ // tup contains three resolved futrues
        std::cout << std::get<0>(tup).available() << "\n";
        std::cout << std::get<1>(tup).get0() << "\n";
        std::cout << std::get<2>(tup).get0() << "\n";
    });
}
seastar::future<> f3() {
    using namespace std::chrono_literals;
    seastar::future<> slow_success = seastar::sleep(1s);
    seastar::future<> slow_exception = seastar::sleep(2s).then([] { throw 1; });
    return seastar::when_all(std::move(slow_success), std::move(slow_exception)
    ).then([] (auto tup) {// in the returned tuple some of the futures may contain an exception instead of a value
        std::cout << std::get<0>(tup).available() << "\n";
        std::cout << std::get<0>(tup).failed() << "\n";//0
        std::cout << std::get<1>(tup).available() << "\n";
        std::cout << std::get<1>(tup).failed() << "\n";//1  uses ready futures which must all be inspected individually for an exception to avoid error messages.
        std::get<1>(tup).ignore_ready_future();
    });
}
seastar::future<> f4() {
    using namespace std::chrono_literals;
    return seastar::when_all_succeed(seastar::sleep(1s), seastar::make_ready_future<int>(2),
                                     seastar::make_ready_future<double>(3.5)
    ).then([] (int i, double d) {
        std::cout << i << " " << d << "\n";
    });
}
seastar::future<> f5() {
    using namespace std::chrono_literals;
    return seastar::when_all_succeed(seastar::make_ready_future<int>(2),
                                     seastar::make_exception_future<double>("oops")
    ).then([] (int i, double d) {
        std::cout << i << " " << d << "\n";
    }).handle_exception([] (std::exception_ptr e) {
        std::cout << "exception: " << e << "\n";
    });
}

seastar::future<> test4() {
//    return f();
//    return f2();
//    return f3();
//    return f4();
    return f5();
}

//semaphore
seastar::future<> g() {
    static thread_local seastar::semaphore limit(10);
    return limit.wait(1).then([] {
        return do_something(12).then([](int number){
            std::cout<<number<<std::endl;
        });
    }).finally([] {
        limit.signal(1);
    });
}
// lambda-based solution to keep `execption safe` of resource acquisition and release
seastar::future<> g2() {
    static thread_local seastar::semaphore limit(100);
    return seastar::with_semaphore(limit, 1, [] {
        return do_something(12).then([](int number){
            std::cout<<number<<std::endl;
        });
    });
}
// Unlike with_semaphore(), the scope of unit holding is not limited to the scope of a single async lambda.
seastar::future<> g3() {
    static thread_local seastar::semaphore limit(100);
    return seastar::get_units(limit, 1).then([] (auto units) {
        return do_something(12).then([units = std::move(units)](int number){
            std::cout<<number<<std::endl;
        }).finally([units=std::move(units)]{
            std::cout<<"finally"<<std::endl;
        });

    });
}

seastar::future<> slow() {
    std::cerr << ".";
    return seastar::sleep(std::chrono::seconds(1));
}
//  millions of sleep() calls might be active in parallel,  Eventually, this loop may consume all available memory and crash
seastar::future<> h() {
    return seastar::repeat([] {
        slow();
        return seastar::stop_iteration::no;
    });
}
//using semaphore to  limit the number of these parallel instances to 10
seastar::future<> h2() {
    return seastar::do_with(seastar::semaphore(10), [] (auto& limit) {
        return seastar::repeat([&limit] {
            return limit.wait(1).then([&limit] {
                seastar::futurize_invoke(slow).finally([&limit] {
                    limit.signal(1);
                });
                return seastar::stop_iteration::no;
            });
        });
    });
}

//using get_units
seastar::future<> h3() {
    return seastar::do_with(seastar::semaphore(10),[](auto& limit){
       return seastar::repeat([&limit]{
           return seastar::get_units(limit,1).then([](auto units){
               slow().finally([units=std::move(units)](){});
               return seastar::stop_iteration::no;
           });
       }) ;
    });
}

// two cases when using loop
seastar::future<> h4() {
    return seastar::do_for_each(boost::counting_iterator<int>(0),
                                boost::counting_iterator<int>(100), [] (int i) {

          // case1 : the call of slow() is one after another , because slow is a `promise`
//        return slow().then([i]{
//            std::cout<<i<<std::endl;
//        });

        // case2:  slow() are run in parallel ,because the slow() is `called in a continuation `
        return seastar::make_ready_future().then([i]{
            std::cout<<i<<std::endl;
            slow();
            });
    });
}

//adding an end to the loop , and waiting for all the background operations to be finished
seastar::future<> h5() {
    return seastar::do_with(seastar::semaphore(10), [] (auto& limit) {
        return seastar::do_for_each(boost::counting_iterator<int>(0),
                                    boost::counting_iterator<int>(100), [&limit] (int i) {
                    return seastar::get_units(limit, 1).then([i] (auto units) {
                        std::cout<<i<<std::endl;
                        // slow() is called in a continuation and use semaphore to control its parallelism
                        slow().finally([units = std::move(units)] {});
                    });
                }).finally([&limit] {
            return limit.wait(10);//waiting for all the background operations to be finished
        });
    });
}

//we want several different loops to use the same semaphore to limit their total parallelism
// using gate
thread_local seastar::semaphore limit(100);
seastar::future<> h6() {
    return seastar::do_with(seastar::gate(), [] (auto& gate) {
        return seastar::do_for_each(boost::counting_iterator<int>(0),
                                    boost::counting_iterator<int>(456), [&gate] (int i) {
                    return seastar::get_units(limit, 1).then([&gate] (auto units) {
                        gate.enter();
                        seastar::futurize_invoke(slow).finally([&gate, units = std::move(units)] {
                            gate.leave();
                        });
                    });
                }).finally([&gate] {
            return gate.close(); // have a gate specific to this loop to help us wait for all ongoing operations to complete
        });
    });
}

seastar::future<> test5() {
//    return g();
//    return g2();
//    return g3();
//    return h();
//    return h2();
//    return h3();
//    return h4();
    return h5();
}

//gate test
seastar::future<> slow(int i) {
    std::cerr << "starting " << i << "\n";
    return seastar::sleep(std::chrono::seconds(10)).then([i] {
        std::cerr << "done " << i << "\n";
    });
}
seastar::future<> i1() {
    return seastar::do_with(seastar::gate(), [] (auto& g) {
        return seastar::do_for_each(boost::counting_iterator<int>(1),
                                    boost::counting_iterator<int>(6),
                                    [&g] (int i) {
                                        seastar::with_gate(g, [i] { return slow(i); });
                                        // wait one second before starting the next iteration
                                        return seastar::sleep(std::chrono::seconds(1));
                                    }).then([&g] {
            seastar::sleep(std::chrono::seconds(1)).then([&g] {
                seastar::with_gate(g, [] { return slow(6); });// This will fail, because it will be after the close() ( 1seconds later)
            });
            return g.close();
        });
    });
}


//An operation can check whether its gate was closed by calling the gate's check() method
seastar::future<> slow2(int i, seastar::gate &g) {
    std::cerr << "starting " << i << "\n";
    return seastar::do_for_each(boost::counting_iterator<int>(0),
                                boost::counting_iterator<int>(10),
                                [&g] (int) {
                                    g.check();// if gate is closed ,this fucntion will trigger a gate_closed_exception ,and make this operation finished
                                    return seastar::sleep(std::chrono::seconds(1));
                                }).finally([i] { // when trigger a gate_closed_exception, the finally block will still be executed
        std::cerr << "done " << i << "\n";
    });
}
seastar::future<> i2() {
    return seastar::do_with(seastar::gate(), [] (auto& g) {
        return seastar::do_for_each(boost::counting_iterator<int>(1),
                boost::counting_iterator<int>(6),
                [&g] (int i) {
                    seastar::with_gate(g, [i,&g] { return slow2(i,g); });
                    // wait one second before starting the next iteration
                    return seastar::sleep(std::chrono::seconds(1));
                }).then([&g] {
                   return g.close();
                });
    });
}

seastar::future<> test6() {
//    return i1();
    return i2();
}

int start(int argc,char** argv){
    seastar::app_template app;
    try {
//        app.run(argc, argv, test1);
//        app.run(argc, argv, test2);
//        app.run(argc, argv, test3);
//        app.run(argc, argv, test4);
//        app.run(argc, argv, test5);
        app.run(argc, argv, test6);
        std::cout<<"app run done"<<std::endl;
    } catch(...) {
        std::cerr << "Couldn't start application: "
                  << std::current_exception() << "\n";
        return 1;
    }
    return 0;
}

int main(int argc,char** argv) {
    return start(argc,argv);
}


