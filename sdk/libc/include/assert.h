#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn)) void __assert_fail(const char* expression, const char* file, unsigned int line, const char* function);

#ifdef __cplusplus
}
#endif

#undef assert
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : __assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif

#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif
