
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>

struct control_block {
  size_t strong_ref_count{0};
  size_t weak_ref_count{0};

  virtual ~control_block() = default;
  virtual void unlink() = 0;

  void dec_weak_ref() {
    --weak_ref_count;
    if (weak_ref_count + strong_ref_count == 0) {
      delete this;
    }
  }

  void dec_strong_ref() {
    --strong_ref_count;
    if (strong_ref_count == 0) {
      unlink();
      if (weak_ref_count == 0) {
        delete this;
      }
    }
  }
};

template <typename T, typename Deleter>
struct ptr_block : control_block, Deleter {
public:
  ptr_block(T* ptr, Deleter d) : ptr(ptr), Deleter(std::move(d)) {}

  void unlink() {
    static_cast<Deleter&>(*this)(ptr);
  }

  ~ptr_block() = default;

private:
  T* ptr;
};

template <typename T>
struct obj_block : control_block {
public:
  obj_block() = delete;

  template <typename... Args>
  explicit obj_block(Args&&... args) {
    new (&stor) T(std::forward<Args>(args)...);
  }

  T* get_data_ptr() {
    return reinterpret_cast<T*>(&stor);
  }

  void unlink() {
    get_data_ptr()->~T();
  }

  ~obj_block() = default;

private:
  std::aligned_storage_t<sizeof(T)> stor;
};

template <typename T>
class weak_ptr;

template <typename T>
class shared_ptr {
public:
  shared_ptr() noexcept : cb(nullptr), ptr(nullptr){};

  shared_ptr(std::nullptr_t) : shared_ptr() {}

  ~shared_ptr() {
    unshare();
  }

  template <class E, class Deleter = std::default_delete<E>>
  explicit shared_ptr(E* ptr_, Deleter D = Deleter()) {
    try {
      cb = new ptr_block<E, Deleter>(ptr_, std::move(D));
    } catch (...) {
      D(ptr_);
      throw;
    }
    ptr = ptr_;
    inc_ref();
  }

  shared_ptr(const shared_ptr& other) noexcept : cb(other.cb), ptr(other.ptr) {
    inc_ref();
  }

  template <class E>
  shared_ptr(const shared_ptr<E>& other) noexcept
      : cb(other.cb), ptr(other.ptr) {
    inc_ref();
  }

  template <class E, class PTR_TYPE>
  shared_ptr(const shared_ptr<E>& other, PTR_TYPE* ptr) noexcept
      : cb(other.cb), ptr(ptr) {
    inc_ref();
  }

  template <class E>
  shared_ptr(const shared_ptr<E>& other, std::nullptr_t) noexcept
      : cb(other.cb), ptr(nullptr) {
    inc_ref();
  }

  shared_ptr& operator=(const shared_ptr& other) noexcept {
    if (*this == other) {
      return *this;
    }
    shared_ptr<T>(other).swap(*this);
    return *this;
  }

  template <class E>
  shared_ptr& operator=(const shared_ptr<E>& other) noexcept {
    if (*this == other) {
      return *this;
    }
    shared_ptr<T>(other).swap(*this);
    return *this;
  }

  shared_ptr(shared_ptr&& other) noexcept : shared_ptr() {
    other.swap(*this);
  }

  template <class E>
  shared_ptr(shared_ptr<E>&& other) noexcept : shared_ptr() {
    other.swap(*this);
  }

  shared_ptr& operator=(shared_ptr&& other) noexcept {
    shared_ptr<T>(std::move(other)).swap(*this);
    return *this;
  }

  template <class E>
  shared_ptr& operator=(shared_ptr<E>&& other) noexcept {
    shared_ptr<T>(std::move(other)).swap(*this);
    return *this;
  }

  T* get() const noexcept {
    return ptr;
  }

  operator bool() const noexcept {
    return ptr != nullptr;
  }

  T& operator*() const noexcept {
    return *ptr;
  }
  T* operator->() const noexcept {
    return ptr;
  }

  std::size_t use_count() const noexcept {
    return cb != nullptr ? cb->strong_ref_count : 0;
  }

  void reset() noexcept {
    shared_ptr().swap(*this);
  }

  template <class E, class Deleter = std::default_delete<E>>
  void reset(E* new_ptr, Deleter d = Deleter()) {
    shared_ptr<T>(new_ptr, std::move(d)).swap(*this);
  }

  void swap(shared_ptr<T>& r) noexcept {
    std::swap(cb, r.cb);
    std::swap(ptr, r.ptr);
  }

  friend weak_ptr<T>;
  template <class E>
  friend struct shared_ptr;

  template <typename E, typename... Args>
  friend shared_ptr<E> make_shared(Args&&... args);

private:
  control_block* cb;
  T* ptr;

  void unshare() {
    if (cb == nullptr) {
      return;
    }
    cb->dec_strong_ref();
  }

  void inc_ref() {
    if (cb != nullptr) {
      ++cb->strong_ref_count;
    }
  }

  explicit shared_ptr(const weak_ptr<T>& r) : cb(r.cb), ptr(r.ptr) {
    inc_ref();
  }

  explicit shared_ptr(obj_block<T>* cb) : cb(cb), ptr(cb->get_data_ptr()) {
    inc_ref();
  }
};

template <class T>
bool operator==(const shared_ptr<T>& lhs, const shared_ptr<T>& rhs) {
  return lhs.get() == rhs.get();
}

template <class T>
bool operator!=(const shared_ptr<T>& lhs, const shared_ptr<T>& rhs) {
  return lhs.get() != rhs.get();
}

template <class T>
bool operator==(const shared_ptr<T>& lhs, std::nullptr_t) noexcept {
  return lhs.get() == nullptr;
}

template <class T>
bool operator==(std::nullptr_t, const shared_ptr<T>& rhs) noexcept {
  return rhs.get() == nullptr;
}

template <class T>
bool operator!=(const shared_ptr<T>& lhs, std::nullptr_t) noexcept {
  return lhs.get() != nullptr;
}

template <class T>
bool operator!=(std::nullptr_t, const shared_ptr<T>& rhs) noexcept {
  return rhs.get() != nullptr;
}

template <typename T>
class weak_ptr {
public:
  weak_ptr() noexcept {
    cb = nullptr;
    ptr = nullptr;
  }

  template <class E>
  weak_ptr(const shared_ptr<E>& other) noexcept : cb(other.cb), ptr(other.ptr) {
    inc_ref();
  }

  template <class E>
  weak_ptr& operator=(const shared_ptr<E>& other) noexcept {
    weak_ptr<T>(other).swap(*this);
    return *this;
  }

  weak_ptr(weak_ptr const& other) noexcept : cb(other.cb), ptr(other.ptr) {
    inc_ref();
  }

  weak_ptr& operator=(weak_ptr const& r) noexcept {
    weak_ptr<T>(r).swap(*this);
    return *this;
  }

  weak_ptr(weak_ptr&& other) : weak_ptr() {
    other.swap(*this);
  }

  weak_ptr& operator=(weak_ptr&& r) noexcept {
    weak_ptr<T>(std::move(r)).swap(*this);
    return *this;
  }

  void swap(weak_ptr& r) noexcept {
    std::swap(cb, r.cb);
    std::swap(ptr, r.ptr);
  }

  ~weak_ptr() {
    unshare();
  }

  size_t use_count() const noexcept {
    return cb == nullptr ? 0 : cb->strong_ref_count;
  }

  bool expired() const noexcept {
    return use_count() == 0;
  }

  shared_ptr<T> lock() const noexcept {
    return expired() ? shared_ptr<T>() : shared_ptr<T>(*this);
  }

  friend shared_ptr<T>;

private:
  control_block* cb{};
  T* ptr;

  void inc_ref() {
    if (cb != nullptr) {
      cb->weak_ref_count++;
    }
  }

  void unshare() {
    if (cb == nullptr) {
      return;
    }
    cb->dec_weak_ref();
  }
};

template <typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
  return shared_ptr<T>(new obj_block<T>(std::forward<Args...>(args...)));
}
