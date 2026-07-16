// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>

namespace ndtbl {

/**
 * @brief Base class for ndtbl-specific runtime failures.
 *
 * Catch this type when all ndtbl format, I/O, and state failures should be
 * handled uniformly.
 */
class Error : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

/**
 * @brief Invalid, unsupported, truncated, or loader-incompatible ndtbl data.
 */
class FormatError : public Error
{
public:
  using Error::Error;
};

/**
 * @brief Filesystem, stream, memory-mapping, or operating-system failure.
 */
class IOError : public Error
{
public:
  using Error::Error;
};

/**
 * @brief Operation requiring a populated ndtbl object was used on empty state.
 */
class StateError : public Error
{
public:
  using Error::Error;
};

} // namespace ndtbl
