//
// Created by Hoppz on 25-12-8.
//

#ifndef LIBZDS_UTILS_STATUS_H
#define LIBZDS_UTILS_STATUS_H

#include <string>
#include <algorithm>

namespace libzds {

class Status {
public:
    /// -------------------- Creator

    /// all below create a success status
    Status() noexcept : state_(nullptr) {}

    ~Status() { delete[] state_;}

    Status(const Status& rhs);
    Status& operator=(const Status& rhs);

    Status(Status&& rhs) noexcept : state_(rhs.state_) { rhs.state_ = nullptr;}
    Status& operator=(Status&& rhs) noexcept;

    /// return a success status
    static Status OK() {return Status();}

    /// return error status of an appropriate type.
    static Status NotFound(const std::string& msg, const std::string& msg2 = std::string()){
        return Status(kNotFound, msg, msg2);
    }

    static Status Corruption(const std::string& msg, const std::string& msg2 = std::string()){
        return Status(kCorruption, msg, msg2);
    }

    static Status NotSupported(const std::string& msg, const std::string& msg2 = std::string()){
        return Status(kNotSupported, msg, msg2);
    }

    static Status InvalidArgument(const std::string& msg, const std::string& msg2 = std::string()){
        return Status(kInvalidArgument, msg, msg2);
    }

    static Status IOError(const std::string& msg, const std::string& msg2 = std::string()){
        return Status(kIOError, msg, msg2);
    }

    /// -------------------- getter

    /// return true iff status indicates success
    bool ok() const {return (state_ == nullptr);}

    /// return true iff status indicates a NotFound error
    bool IsNotFound() const {return code() == kNotFound;}

    /// return true iff status indicates a Corruption error
    bool IsCorruption() const {return code() == kCorruption;}

    /// return true iff status indicates a IOError error
    bool IsIOError() const {return code() == kIOError;}

    /// return true iff status indicates a NotSupported error
    bool IsNotSupportedError() const {return code() == kNotSupported;}

    /// return true iff status indicates a InvalidArgument error
    bool IsInvalidArgument() const {return code() == kInvalidArgument;}

    std::string ToString() const;

private:
    enum Code
    {
        kOk = 0,
        kNotFound = 1,
        kCorruption = 2,
        kNotSupported = 3,
        kInvalidArgument = 4,
        kIOError = 5,
    };

    Code code() const {
        return (state_ == nullptr) ? kOk  : static_cast<Code>(state_[4]);
    }

    Status (Code code, const std::string& msg, const std::string& msg2);

    /// copy s into a new const char * ptr, and return the new ptr
    static const char* CopyState(const char* s);

    /// OK status has a null state_. Otherwise, state_ is a new[] array
    /// of the following form:
    ///     state_[0..3] == length of message -> uint32_t
    ///     state_[4]    == code
    ///     state_[5]    == message;
    const char* state_;

};

inline Status::Status(const Status& rhs){
    state_ = (rhs.state_ == nullptr) ? nullptr : CopyState(rhs.state_);
}

inline Status &Status::operator=(const Status &rhs)
{
    // The following condition catches both aliasing (when this == &rhs),
    // and the common case where both rhs and *this are ok.
    if ( state_ != rhs.state_ ) {
        delete[] state_;
        state_ = (rhs.state_ == nullptr) ? nullptr : CopyState(rhs.state_);
    }
    return *this;
}

inline Status &Status::operator=(Status &&rhs) noexcept
{
    /// 交换指针比很多参数的交换要快很多
    /// 把当前对象的 state_ 交换出去会使得当前对象分配的内存
    /// 随着移动对象的析构而释放
    ///
    /// 使用 std::swap 同时也会自动避免自己移动到自己的问题
    std::swap(state_, rhs.state_);
    return *this;
}

} // namespace libzds


#endif // LIBZDS_UTILS_STATUS_H
