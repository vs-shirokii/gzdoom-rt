#ifndef UT_DEFER_HPP
#define UT_DEFER_HPP

/* Usage:
 * std >= c++17
 *
 * ```cpp
 * {
 *     auto res = make_resource();
 *     // gets called at the end of the scope
 *     defer { free_resource(res); };
 *
 *     // use res
 *
 *     auto child = res.make_child_resource();
 *     // defers are invoked in reverese order, so child is freed before parent
 *     defer { free_resource(child); };
 * }
 * ```
 *
 * NOTE: All exceptions thrown in a defer statement should be caught in the defer statement. If they are
 *       not, they cannot be caught and will result in a call to std::terminate
 *
 *  *THIS DOES NOT WORK*
 *
 * ```cpp
 *  {
 *     try{
 *         defer { throw 1; };
 *     catch(...){
 *         // Will not be invoked
 *     }
 *  }
 *  ```
 */

namespace ut::detail {
template<typename Fn>
class Defer {
    Fn m_fn;
public:
    [[nodiscard(
        "discarding the result of this constructor will result in the deferred statement being immediatly executed")]]  //
    Defer(Fn &&fn) noexcept  // guaranteed to be noexcept, since it only ever gets [&] lambda passed
                             // to it not using std::move to avoid including extra headers
            : m_fn(static_cast<Fn &&>(fn)) { }

    ~Defer() noexcept  // if it throws it can't be caught anyway
    {
        m_fn();
    }
};
template<class T>
Defer(T &&) -> Defer<T>;
}  // namespace ut::detail

#define UT_DEFER_merge(x, y) x##y
#define UT_DEFER_eval(x, y)  UT_DEFER_merge(x, y)
#define defer                ut::detail::Defer const UT_DEFER_eval(defer_, __LINE__) = [&]() noexcept

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org>
*/
#endif  // UT_DEFER_HPP
