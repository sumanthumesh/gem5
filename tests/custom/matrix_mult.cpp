#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <random>
#include <chrono>

// Function to generate a random matrix
std::vector<std::vector<int>> generateMatrix(int rows, int cols) {
    std::vector<std::vector<int>> matrix(rows, std::vector<int>(cols));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 100);

    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            matrix[i][j] = dis(gen);
        }
    }
    return matrix;
}

// Function to multiply a submatrix
void multiplySubMatrix(const std::vector<std::vector<int>>& A, 
                       const std::vector<std::vector<int>>& B,
                       std::vector<std::vector<int>>& C,
                       int startRow, int endRow) {
    int colsB = B[0].size();
    int colsA = A[0].size();

    for (int i = startRow; i < endRow; ++i) {
        for (int j = 0; j < colsB; ++j) {
            int sum = 0;
            for (int k = 0; k < colsA; ++k) {
                sum += A[i][k] * B[k][j];
            }
            C[i][j] = sum;
        }
    }
}

int main(int argc, char **argv) {
    const int rows = std::atoi(argv[1]);
    const int cols = std::atoi(argv[1]);
    const int numThreads = std::atoi(argv[2]);
    std::cout<<"B1\n";

    // Generate matrices
    auto A = generateMatrix(rows, cols);
    auto B = generateMatrix(cols, rows);
    std::cout<<"B2\n";
    std::vector<std::vector<int>> C(rows, std::vector<int>(rows, 0));

    // Start timing
    auto start = std::chrono::high_resolution_clock::now();

    std::cout<<"B3\n";
    // Create threads for parallel multiplication
    std::vector<std::thread> threads;
    int rowsPerThread = rows / numThreads;
    for (int i = 0; i < numThreads; ++i) {
        int startRow = i * rowsPerThread;
        int endRow = (i == numThreads - 1) ? rows : startRow + rowsPerThread;
        threads.emplace_back(multiplySubMatrix, std::cref(A), std::cref(B), std::ref(C), startRow, endRow);
    }


    std::cout<<"B4\n";
    // Join threads
    for (auto& thread : threads) {
        thread.join();
    }

    // End timing
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Matrix multiplication completed in " << elapsed.count() << " seconds." << std::endl;
    std::cout<<"B5\n";

    return 0;
}
