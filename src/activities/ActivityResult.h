#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

// Result payload passed back from a pushed sub-activity to whoever launched it via
// startActivityForResult(). Kept intentionally tiny — Skinwalker's settings/lore screens are
// simple enough that most Activities never need to return anything beyond "which row did the
// user pick".
struct OptionSelectionResult {
  uint8_t index = 0;
};

using ResultVariant = std::variant<std::monostate, OptionSelectionResult>;

struct ActivityResult {
  bool isCancelled = false;
  ResultVariant data;

  explicit ActivityResult() = default;

  template <typename ResultType>
    requires std::is_constructible_v<ResultVariant, ResultType&&>
  // cppcheck-suppress noExplicitConstructor
  ActivityResult(ResultType&& result) : data{std::forward<ResultType>(result)} {}
};

using ActivityResultHandler = std::function<void(const ActivityResult&)>;
