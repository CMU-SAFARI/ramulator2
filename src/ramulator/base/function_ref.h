#ifndef RAMULATOR_BASE_FUNCTION_REF_H
#define RAMULATOR_BASE_FUNCTION_REF_H

#include <memory>
#include <utility>

namespace Ramulator {

template <typename Signature>
class FunctionRef;

template <typename Ret, typename... Args>
class FunctionRef<Ret(Args...)> {
 public:
  FunctionRef() = default;

  // Non-owning callable view. The referenced callable must outlive this
  // FunctionRef and any invocation made through it.
  template <typename F>
  FunctionRef(const F& fn)
      : m_obj(static_cast<const void*>(std::addressof(fn))),
        m_call([](const void* obj, Args... args) -> Ret {
          return (*static_cast<const F*>(obj))(std::forward<Args>(args)...);
        }) {
  }

  Ret operator()(Args... args) const {
    return m_call(m_obj, std::forward<Args>(args)...);
  }

  explicit operator bool() const {
    return m_call != nullptr;
  }

 private:
  const void* m_obj = nullptr;
  Ret (*m_call)(const void*, Args...) = nullptr;
};

}  // namespace Ramulator

#endif  // RAMULATOR_BASE_FUNCTION_REF_H
