
#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>

/**
 * Lifetime management
 *       asynchronous operation needs to operate on existing (temporary) objects,
 *       We need to ensure that these objects do not get destroyed before the asynchronous function completes,and to also ensure that the object finally get destroyed when it is no longer needed
 *       Seastar mechanisms:
 *             1. Passing ownership to continuation : using lambda capture list, copying(small objs) or moving (large objs )
 *             2. Keeping ownership at the caller : using lambda capture list  , reference , seastar::do_with
 *                    Passing ownership to continuation will  becomes hard and verbose When an asynchronous operation involves a chain of continations that each needs to work on the same object
 *             3. Sharing ownership:  seastar::shared_ptr, seastar::lw_shared_ptr
 *                std::shared_ptr<T> was designed with multi-threaded applications and  it uses slow atomic increment/decrement operations for the reference count which is unnecessary in Seastar  (Seastar objects are always used by a single CPU only)
 *                seastar::shared_ptr<T>. :is similar to std::shared_ptr<T> except no atomic operations are used
 *                seastar::lw_shared_ptr<T>: does not support polymorphic types compared with  seastar::shared_ptr<T>
 */
 class A{
 public:
     int aa=10;
     A(){
         std::cout<<" cons"<<std::endl;
     }
     A(const A& a){
         std::cout<<"copy cons"<<std::endl;
     }
     A(A&& a) noexcept {// move constructor must be `noexecpt`
         std::cout<<"move cons"<<std::endl;
     }


 };
template<typename T>
seastar::future<> slow_op(T&& o) {
    using namespace std::chrono_literals;
    return seastar::sleep(10ms).then([o = std::move(o)] {
        //after we move the object into one continuation, the object needs to be returned so it can be moved again into the second continuation
        return std::move(o);
    }).then([](T&& o){
        std::cout<<o.aa<<std::endl;
    });
}
seastar::future<> test1(){
    return slow_op(A());
}

// use do_with & lambda reference capture
//Whenever an asynchronous function takes a parameter by reference, the caller must ensure that the referred object lives until the future returned by the function is resolved  (seastar::do_with to ensure it !)
// using reference capture
template<typename  T>
seastar::future<> slow_op2(T& o) { //pass by reference
    using namespace std::chrono_literals;
    return seastar::sleep(10ms).then([&o] {// capture by reference
        std::cout<<"first continuation "<<o.aa<<std::endl;
    }).then([&o]{  // another capture by reference
        std::cout<<"second continuation "<<o.aa<<std::endl;
    });
}
template <typename  T>
seastar::future<> test2() { // wrong!!
    T obj; // wrong! will be destroyed too soon, and the address of obj is invalid when execute the asynchornized fucntion slow_op2
    return slow_op2(obj);
}

// `do_with`  will do the given function with the given object alive
//do_with saves the given object on the `heap`, and calls the given lambda with a `reference` to the new object.
// Finally it ensures that the new object is destroyed after the returned future is resolved
//The asynchronous function which we call inside do_with must not use the objects held by do_with after the returned future is resolved ,which will cause use-after-free bug

template <typename  T>
seastar::future<> test3() {
    return seastar::do_with(T(), [] (T& obj) { //do_with is given an rvalue eg: T();  if the lambda parameter is` T obj`, the obj in lambda is a copy, and will destory when the  lambda function return
        // obj is passed by reference to slow_op, and this is fine:
        return slow_op2(obj);
    });
}
template <typename  T>

// do_with can receive multiple parameters
seastar::future<> test4() {
    return seastar::do_with(T(), T(), [] (auto& obj1, auto& obj2) {
        return seastar::make_ready_future();
    });
}


int start(int argc,char** argv){
    seastar::app_template app;
    try {
//        app.run(argc, argv, test1);
//        app.run(argc, argv, test2<A>);
        app.run(argc, argv, test3<A>);
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


