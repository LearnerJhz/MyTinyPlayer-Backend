#pragma once
// 定义 DEBUG_PRINT 宏来开启调试输出（注释掉此行即可关闭）
#define DEBUG_PRIN

#ifdef DEBUG_PRINT
#define DEBUG_COUT std::cout
#define DEBUG_CERR std::cerr
#else
// 非调试模式下，将输出重定向到空操作
#define DEBUG_COUT if (false) std::cout
#define DEBUG_CERR if (false) std::cerr
#endif

// 控制锁 
#define USE_DUMMY_LOC

#ifdef USE_DUMMY_LOCK
using LockType = DummyLock;
#else
#include <mutex>
using LockType = std::mutex;
#endif

