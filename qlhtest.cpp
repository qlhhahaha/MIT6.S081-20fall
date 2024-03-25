#include <iostream>
#include <vector>
#include <numeric>
// 计算两个数组的和
int sum(const std::vector<int>& arr) {
    return std::accumulate(arr.begin(), arr.end(), 0);
}

// 计算互补子序列的数量
int countComplementarySubsets(int n, const std::vector<int>& a, const std::vector<int>& b) {
    int sumA = sum(a);
    int sumB = sum(b);
    int halfSum = (sumA + sumB) / 2; // 和的一半，用于判断是否互补
    int count = 0;

    // 计算所有可能的子序列数量
    long long totalSubsets = 1LL << n; // 2^n 个子序列

    // 对于每个子序列，检查是否互补
    for (int i = 0; i < totalSubsets; ++i) {
        int subsetSumA = 0, subsetSumB = 0;
        for (int j = 0; j < n; ++j) {
            if (i & (1 << j)) { // 如果二进制位为1，选择a中的元素
                subsetSumA += a[j];
            }
            else { // 否则选择b中的元素
                subsetSumB += b[j];
            }
        }
        // 如果两个子序列的和互补，则计数
        if (subsetSumA == halfSum && subsetSumB == halfSum) {
            ++count;
        }
    }

    return count;
}

int main() {
    int n;
    std::cin >> n;
    std::vector<int> a(n), b(n);

    // 读取数组a和b的元素
    for (int i = 0; i < n; ++i) {
        std::cin >> a[i];
    }
    for (int i = 0; i < n; ++i) {
        std::cin >> b[i];
    }

    // 计算并输出互补子序列的数量
    std::cout << countComplementarySubsets(n, a, b) << std::endl;

    return 0;
}
