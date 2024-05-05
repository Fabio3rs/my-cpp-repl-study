#include "printerOverloads.hpp"
#include "stdafx.hpp"

void writeHeaderPrintOverloads() {
    auto printer = R"cpp(#pragma once
#include <deque>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

template <class T>
inline void printdata(const std::vector<T> &vect, std::string_view name,
                      std::string_view type) {
    std::cout << " >> " << type << (name.empty() ? "" : " ")
              << (name.empty() ? "" : name) << ": ";
    for (const auto &v : vect) {
        std::cout << v << ' ';
    }

    std::cout << std::endl;
}

template <class T>
inline void printdata(const std::deque<T> &vect, std::string_view name,
                      std::string_view type) {
    std::cout << " >> " << type << (name.empty() ? "" : " ")
              << (name.empty() ? "" : name) << ": ";
    for (const auto &v : vect) {
        std::cout << v << ' ';
    }

    std::cout << std::endl;
}

inline void printdata(std::string_view str, std::string_view name,
                      std::string_view type) {
    std::cout << " >> " << type << (name.empty() ? "" : " ")
              << (name.empty() ? "" : name) << ": " << str << std::endl;
}

inline void printdata(const std::mutex &mtx, std::string_view name,
                      std::string_view type) {
    std::cout << " >> " << (name.empty() ? "" : " ")
              << (name.empty() ? "" : name) << "Mutex" << std::endl;
}

template <class T> struct is_printable {
    static constexpr bool value =
        std::is_same_v<decltype(std::cout << std::declval<T>()),
                       std::ostream &>;
};

template <class K, class V>
inline void printdata(const std::unordered_map<K, V> &map,
                      std::string_view name, std::string_view type) {
    if constexpr (is_printable<K>::value && is_printable<V>::value) {
        std::cout << " >> " << type << (name.empty() ? "" : " ")
                  << (name.empty() ? "" : name) << ": ";
        for (const auto &m : map) {
            std::cout << m.first << " : " << m.second << ' ';
        }
        std::cout << std::endl;
    } else if constexpr (is_printable<K>::value) {
        std::cout << " >> " << type << (name.empty() ? "" : " ")
                  << (name.empty() ? "" : name) << ": ";
        for (const auto &m : map) {
            std::cout << m.first << " : "
                      << "Not printable" << ' ';
        }
        std::cout << std::endl;
    } else if constexpr (is_printable<V>::value) {
        std::cout << " >> " << type << (name.empty() ? "" : " ")
                  << (name.empty() ? "" : name) << ": ";
        for (const auto &m : map) {
            std::cout << "Not printable"
                      << " : " << m.second << ' ';
        }
        std::cout << std::endl;
    } else {
        std::cout << " >> " << type << (name.empty() ? "" : " ")
                  << (name.empty() ? "" : name) << ": "
                  << "Not printable with " << map.size() << " elements"
                  << std::endl;
    }
}

template <class T>
inline void printdata(const T &val, std::string_view name,
                      std::string_view type) {
    if constexpr (is_printable<T>::value) {
        std::cout << " >> " << type << (name.empty() ? "" : " ")
                  << (name.empty() ? "" : name) << ": " << val << std::endl;
    } else {
        std::cout << " >> " << type << (name.empty() ? "" : " ")
                  << (name.empty() ? "" : name) << ": "
                  << "Not printable" << std::endl;
    }
}

)cpp";

    std::fstream printerOutput("printerOutput.hpp",
                               std::ios::out | std::ios::trunc);

    printerOutput << printer << std::endl;

    printerOutput.close();
}
