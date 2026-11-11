#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace rbush {
// Literal translation of quickselect 0711310d28fe5f8a5ca75c721626d8e49feb9ccb.
// Signed indexes preserve the source algorithm's right = j - 1 boundary.
template <class T, class Compare>
void quickselect(std::vector<T>& arr, std::ptrdiff_t k,
                 std::ptrdiff_t left, std::ptrdiff_t right, Compare compare) {
    while (right > left) {
        if (right - left > 600) {
            const double n = static_cast<double>(right - left + 1);
            const double m = static_cast<double>(k - left + 1);
            const double z = std::log(n);
            const double s = 0.5 * std::exp(2 * z / 3);
            const double sd = 0.5 * std::sqrt(z * s * (n - s) / n) * (m - n / 2 < 0 ? -1 : 1);
            const auto newLeft = std::max(left, static_cast<std::ptrdiff_t>(std::floor(k - m * s / n + sd)));
            const auto newRight = std::min(right, static_cast<std::ptrdiff_t>(std::floor(k + (n - m) * s / n + sd)));
            quickselect(arr, k, newLeft, newRight, compare);
        }
        const T t = arr[k];
        auto i = left;
        auto j = right;
        std::swap(arr[left], arr[k]);
        if (compare(arr[right], t) > 0) std::swap(arr[left], arr[right]);
        while (i < j) {
            std::swap(arr[i], arr[j]);
            i++;
            j--;
            while (compare(arr[i], t) < 0) i++;
            while (compare(arr[j], t) > 0) j--;
        }
        if (compare(arr[left], t) == 0) std::swap(arr[left], arr[j]);
        else {
            j++;
            std::swap(arr[j], arr[right]);
        }
        if (j <= k) left = j + 1;
        if (k <= j) right = j - 1;
    }
}

template <class T>
void quickselect(std::vector<T>& arr, std::ptrdiff_t k,
                 std::ptrdiff_t left, std::ptrdiff_t right) {
    quickselect(arr, k, left, right, [](const T& a, const T& b) {
        return a < b ? -1 : a > b ? 1 : 0;
    });
}

template <class T>
void quickselect(std::vector<T>& arr, std::ptrdiff_t k) {
    quickselect(arr, k, 0, static_cast<std::ptrdiff_t>(arr.size()) - 1);
}
} // namespace rbush
