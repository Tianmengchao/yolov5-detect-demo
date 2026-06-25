#include "tracker/hungarian.h"
#include <algorithm>
#include <limits>
#include <cmath>

// Kuhn-Munkres (Hungarian) algorithm for rectangular cost matrices
std::vector<int> hungarianSolve(const std::vector<std::vector<float>>& cost_matrix,
                                float max_cost) {
    if (cost_matrix.empty()) return {};

    int n_rows = static_cast<int>(cost_matrix.size());
    int n_cols = static_cast<int>(cost_matrix[0].size());
    int n = std::max(n_rows, n_cols);

    // Pad to square matrix with max_cost
    std::vector<std::vector<float>> C(n, std::vector<float>(n, max_cost));
    for (int i = 0; i < n_rows; i++) {
        for (int j = 0; j < n_cols; j++) {
            C[i][j] = cost_matrix[i][j];
        }
    }

    // Row reduction
    for (int i = 0; i < n; i++) {
        float min_val = *std::min_element(C[i].begin(), C[i].end());
        for (int j = 0; j < n; j++) C[i][j] -= min_val;
    }

    // Column reduction
    for (int j = 0; j < n; j++) {
        float min_val = std::numeric_limits<float>::max();
        for (int i = 0; i < n; i++) min_val = std::min(min_val, C[i][j]);
        for (int i = 0; i < n; i++) C[i][j] -= min_val;
    }

    // Greedy assignment on zeros (simplified for typical small matrices in tracking)
    std::vector<int> row_assign(n, -1);
    std::vector<int> col_assign(n, -1);

    // Iterative augmenting path
    for (int iter = 0; iter < n * n; iter++) {
        // Find unassigned row with a zero
        int found_row = -1;
        int found_col = -1;
        float min_uncovered = std::numeric_limits<float>::max();

        for (int i = 0; i < n; i++) {
            if (row_assign[i] != -1) continue;
            for (int j = 0; j < n; j++) {
                if (col_assign[j] != -1) continue;
                if (C[i][j] < min_uncovered) {
                    min_uncovered = C[i][j];
                    found_row = i;
                    found_col = j;
                }
            }
        }

        if (found_row == -1) break;

        if (min_uncovered > 1e-6f) {
            // Subtract min from unassigned rows, add to assigned columns
            for (int i = 0; i < n; i++) {
                if (row_assign[i] == -1) {
                    for (int j = 0; j < n; j++) {
                        C[i][j] -= min_uncovered;
                    }
                }
            }
            for (int j = 0; j < n; j++) {
                if (col_assign[j] != -1) {
                    for (int i = 0; i < n; i++) {
                        C[i][j] += min_uncovered;
                    }
                }
            }
            continue;
        }

        row_assign[found_row] = found_col;
        col_assign[found_col] = found_row;
    }

    // Extract results, filter by original cost threshold
    std::vector<int> result(n_rows, -1);
    for (int i = 0; i < n_rows; i++) {
        int j = row_assign[i];
        if (j >= 0 && j < n_cols && cost_matrix[i][j] < max_cost) {
            result[i] = j;
        }
    }

    return result;
}
