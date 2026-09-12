#ifndef LEVEL_DATA_H
#define LEVEL_DATA_H

#include <cstddef>

#include "managers.h"

namespace LevelData {

/**
 * @brief Retrieves armada convoy data for the specified combat stage (1-based
 * index).
 */
const ConvoyData* GetConvoyData(size_t level_number);

/**
 * @brief Returns total number of distinct progressive levels before repeating
 * with difficulty scaling.
 */
size_t GetTotalLevels();

}  // namespace LevelData

#endif  // LEVEL_DATA_H
