cmake_minimum_required(VERSION 3.15)
project(seastarBase)

set(CMAKE_CXX_STANDARD 17)

find_package (Seastar REQUIRED)

#add_executable (seastarBase main.cpp D1_threads_memory_future_continuation_execption/HelloWorld1.cc)
#add_executable (seastarBase D1_threads_memory_future_continuation_execption/HelloWorld1.cc D2_lifeTimeManage_ownershipControl/HelloWorld2.cc)
#add_executable (seastarBase D2_lifeTimeManage_ownershipControl/HelloWorld2.cc D3_loops_repeat_doUntil_doForEach_whenALL_semaphore_/HelloWorld3.cc)
#add_executable (seastarBase D3_loops_repeat_doUntil_doForEach_whenALL_semaphore_/HelloWorld3.cc)
add_executable (seastarBase D4_network/HelloWorld4.cc)

target_link_libraries (seastarBase Seastar::seastar)