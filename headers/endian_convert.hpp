#pragma once
#include <cstddef>
#include <cstdint>
#include <endian.h>
#include <type_traits>

namespace endian_convert {

  template <typename T>
  union type64 {
    uint64_t data_uint;
    std::enable_if_t<sizeof(T) == sizeof(uint64_t), T> data;

    constexpr T hton() {
      data_uint = htobe64(data_uint);
      return data;
    }

    constexpr T ntoh() {
      data_uint = be64toh(data_uint);
      return data;
    }
  };

  template <typename T>
  union type32 {
    uint32_t data_uint;
    std::enable_if_t<sizeof(T) == sizeof(uint32_t), T> data;

    constexpr T hton() {
      data_uint = htobe32(data_uint);
      return data;
    }

    constexpr T ntoh() {
      data_uint = be32toh(data_uint);
      return data;
    }
  };

  template <typename T>
  union type16 {
    uint16_t data_uint;
    std::enable_if_t<sizeof(T) == sizeof(uint16_t), T> data;
    constexpr T hton() {
      data_uint = htobe16(data_uint);
      return data;
    }

    constexpr T ntoh() {
      data_uint = be16toh(data_uint);
      return data;
    }
  };


  template <size_t S>
  struct converter;

  template <>
  struct converter<sizeof(uint64_t)>
  {
    template <typename T>
    using type = type64<T>;
  };

  template <>
  struct converter<sizeof(uint32_t)>
  {
    template <typename T>
    using type = type32<T>;
  };

  template <>
  struct converter<sizeof(uint16_t)>
  {
    template <typename T>
    using type = type16<T>;
  };

  template <typename T>
  constexpr T hton(const T& v) {
    using type = typename converter<sizeof(T)>::template type<T>;
    type temp;
    temp.data = v;
    return temp.hton();
  }

  template <typename T>
  constexpr T ntoh(const T& v) {
    using type = typename converter<sizeof(T)>::template type<T>;
    type temp;
    temp.data = v;
    return temp.ntoh();
  }
}
