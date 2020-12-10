#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>

int main(int argc,char** argv) {
    // This  app_template object starts the main event loop on one or more CPUs, and then runs the given lambda function
    seastar::app_template app;
    app.run(argc,argv,[](){
        std::cout<<"hello seastar "<<std::endl;
        return seastar::make_ready_future();  //make event loop, and the whole application exit
    });
}