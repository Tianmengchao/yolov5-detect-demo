#pragma once

#include <vector>

// 匈牙利算法求解线性分配问题
// cost_matrix[i][j] = 将行 i 分配给列 j 的代价
// 返回: matches[i] = 行 i 分配到的列号，-1 表示未分配
std::vector<int> hungarianSolve(const std::vector<std::vector<float>>& cost_matrix,
                                float max_cost);
