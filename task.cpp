#include <process.h>
#include <windows.h>
#include <thread>
#include <stdio.h>
#include <vector>
#include <mutex>
#include <iostream>

using namespace std;

#define IN
#define OUT

#define RETURN_OK 0
#define RETURN_ERROR 1

// 数据结构
typedef struct {
    int32_t a;
    int32_t b;
} Row;

/* 这里最好用py随机生成一定量的数据，时间有限，直接硬编码了 */

// 随机数据
Row unsortedRow[] = {
    {1500, 12},
    {1000, 32},
    {2000, 22},
    {1000, 33},
    {1000, 31},
    {1000, 34},
    {1000, 72},
    {1500, 34},
    {2000, 33},
    {3000, 11},
};

// 有序数据
Row sortedRow[] = {
    {1000, 31},
    {1000, 32},
    {1000, 33},
    {1000, 34},
    {1000, 72},
    {1500, 12},
    {1500, 34},
    {2000, 22},
    {2000, 33},
    {3000, 22},
};

// 线程入参
typedef struct {
    Row *startPos;   // 线程处理的row数组的起始位置
    uint32_t rowNum; // 线程处理的row的数量
} rowRange;

// 入参检查
int32_t TaskParamCheck(Row* rows, uint32_t nRows)
{
    if (rows == NULL || nRows == 0) {
        return RETURN_ERROR;
    }
    return RETURN_OK;
}

// 二分查找，找到 a == x * 1000 && b >= 10 && b < 50 的区间范围
void binaryPosFind(IN int32_t a, IN Row* rows, IN uint32_t nRows, OUT rowRange* range)
{
    Row target = { 0 };
    uint32_t leftPos, rightPos;

    // 1. 先找到a的范围区间
    target.a = a;
    leftPos = lower_bound(rows, rows + nRows, target, [](const Row& row1, const Row& row2) { return row1.a < row2.a; }) - rows;
    rightPos = upper_bound(rows, rows + nRows, target, [](const Row& row1, const Row& row2) { return row1.a < row2.a; }) - rows;

    // 2. 找到b >= 10 && b < 50 的范围区间
    target.b = 10;
    leftPos = lower_bound(rows + leftPos, rows + rightPos, target, [](const Row& row1, const Row& row2) { return row1.b < row2.b; }) - rows;
    target.b = 50;
    rightPos = lower_bound(rows + leftPos, rows + rightPos, target, [](const Row& row1, const Row& row2) { return row1.b < row2.b; }) - rows;

    // 3. 输出返回值
    range->startPos = rows + leftPos;
    range->rowNum = rightPos - leftPos;
    return;
}

// task1的线程中执行的函数，将所得数据判断后打印
unsigned int __stdcall Print4Task1(LPVOID param)
{
    rowRange* data = (rowRange*)param;
    int32_t a, b;
    for (uint32_t i = 0; i < data->rowNum; i++) {
        a = data->startPos[i].a;
        b = data->startPos[i].b;
        if (a % 1000 == 0 && b >= 10 && b < 50) {
            printf("%d, %d\n", data->startPos[i].a, data->startPos[i].b);
        }
    }
    return RETURN_OK;
}

// task2的线程中执行的函数，将所得数据直接打印
unsigned int __stdcall Print4Task2(LPVOID param)
{
    rowRange* data = (rowRange*)param;
    int32_t a, b;
    for (uint32_t i = 0; i < data->rowNum; i++) {
        a = data->startPos[i].a;
        b = data->startPos[i].b;
        printf("%d, %d\n", data->startPos[i].a, data->startPos[i].b);
    }
    return RETURN_OK;
}

// task3 桶排序用到的桶和相应互斥锁
vector<vector<Row*>> g_Print4Task3(50);
mutex g_Lock4Task3[50];

// task3的线程中执行的函数，将所得数据区间进行桶排序
unsigned int __stdcall Print4Task3(LPVOID param)
{
    rowRange* data = (rowRange*)param;
    Row* startPos = data->startPos;

    for (uint32_t i = 0; i < data->rowNum; i++) {
        int32_t b = startPos[i].b;
        g_Lock4Task3[b].lock(); // 此处最好使用乐观锁 > 自旋锁 > 互斥锁
        g_Print4Task3[b].push_back(&(startPos[i]));
        g_Lock4Task3[b].unlock();
    }
    return RETURN_OK;
}

// task1实现：多线程输出
#define MIN_THREAD_NUM 1 // 最小线程数
#define MIN_BATCH 2  // 每个线程最少处理的数据量
#define THREAD_HANDLE_MAXIMUM_NUM 256 // 最大线程数

void task1(Row* rows, uint32_t nRows)
{
    if (TaskParamCheck(rows, nRows) != RETURN_OK) {
        cout << "param check failed for task1." << endl;
        return;
    }

    // 1. 计算core的数量
    uint32_t nCore = thread::hardware_concurrency();
    // 1.1 留一个线程
    if (nCore > MIN_THREAD_NUM) {
        nCore--;
    }

    // 2. 判断多少线程数合适
    uint32_t nThread = 0;
    if (nRows <= MIN_BATCH) {
        nThread = 1;
    } else if (nRows > (MIN_BATCH * nCore)) {
        nThread = nCore;
    } else {
        if (nRows % MIN_BATCH == 0) {
            nThread = nRows / MIN_BATCH;
        } else {
            nThread = nRows / MIN_BATCH + 1;
        }
    }

    // 3. 计算每个线程承载的最大数据量
    uint32_t dataPerThread = 0;
    if (nRows > (MIN_BATCH * nCore)) {
        if (nRows % nThread == 0) {
            dataPerThread = nRows / nThread;
        } else {
            dataPerThread = nRows / nThread + 1;
        }
    } else {
        dataPerThread = MIN_BATCH;
    }

    uint32_t currNumSize = nRows;
    Row *currDataPos = &(rows[0]);

    // 5. 循环生成线程(此处为windows api)
    HANDLE handle[THREAD_HANDLE_MAXIMUM_NUM] = { 0 };
    rowRange data[THREAD_HANDLE_MAXIMUM_NUM] = { 0 };

    for (uint32_t i = 0; i < nThread; i++) {
        // 5.1 构造线程数据
        data[i].rowNum = min(currNumSize, dataPerThread);
        data[i].startPos = currDataPos;
        // 5.2 生成线程
        handle[i] = (HANDLE)_beginthreadex(NULL, 0, Print4Task1, (LPVOID)&(data[i]), 0, NULL);
        if (handle[i] == 0) {
            cout << "_beginthreadex failed." << endl;
            return;
        }

        // 5.3 设置线程亲和
        (void)SetThreadAffinityMask(handle[i], static_cast<DWORD_PTR>(1) << i);
        currNumSize -= data[i].rowNum;
        currDataPos += data[i].rowNum;
    }

    // 6. 阻塞主进程，等待线程结束
    WaitForMultipleObjects(nThread, handle, TRUE, INFINITE);
    return;
}

// task2实现：先二分查找，之后将得到的数据区间多线程并发输出即可
void task2(Row* rows, uint32_t nRows)
{
    rowRange rowPos[3] = { 0 };
    HANDLE handle[3] = { 0 };
    int32_t index;

    if (TaskParamCheck(rows, nRows) != RETURN_OK) {
        cout << "param check failed for task2." << endl;
        return;
    }

    // 1. 二分法查找数据范围，然后放入线程中执行
    for (int32_t a = 1000; a <= 3000; a += 1000) {
        index = a / 1000 - 1;
        binaryPosFind(a, rows, nRows, &(rowPos[index]));
        handle[index] = (HANDLE)_beginthreadex(NULL, 0, Print4Task2, (LPVOID) & (rowPos[index]), 0, NULL);
        if (handle[index] == 0) {
            cout << "_beginthreadex failed in task2." << endl;
            break;
        }
    }

    //  2. 阻塞主进程，等待线程结束
    WaitForMultipleObjects(3, handle, TRUE, INFINITE);
    return;
}

// task3实现：先二分查找，之后各线程将所得数据放入b的桶中（桶排序），之后主线程等待从线程结束后依次打印即可
void task3(Row* rows, uint32_t nRows)
{
    rowRange rowPos[3] = { 0 };
    HANDLE handle[3] = { 0 };
    int32_t index;

    if (TaskParamCheck(rows, nRows) != RETURN_OK) {
        cout << "param check failed for task2." << endl;
        return;
    }

    // 1. 二分法查找数据范围，然后放入线程中执行
    for (int32_t a = 1000; a <= 3000; a += 1000) {
        index = a / 1000 - 1;
        binaryPosFind(a, rows, nRows, &(rowPos[index]));
        handle[index] = (HANDLE)_beginthreadex(NULL, 0, Print4Task3, (LPVOID) & (rowPos[index]), 0, NULL);
        if (handle[index] == 0) {
            cout << "_beginthreadex failed in task2." << endl;
            return;
        }
    }

    // 2. 阻塞主进程，等待线程结束
    WaitForMultipleObjects(3, handle, TRUE, INFINITE);

    // 3. 依次打印出桶内元素，即按照b排序
    for (int32_t i = 10; i < 50; i++) {
        vector<Row*> currBucket = g_Print4Task3[i];
        for (int32_t j = 0; j < currBucket.size(); j++) {
            printf("%d, %d\n", currBucket[j]->a, currBucket[j]->b);
        }
    }
    return;
}

/* 
 * task4: 思路：在task3的基础上，先二分法划分出99个数据区间（99000 / 1000）,
 * 将99个数据区间均匀打散到各个线程中去，每个线程将归属的数据区间遍历进行桶排序，
 * 之后主线程等待从线程结束后，依次打印桶中元素即可。有一定代码量，就不在这里实现了，
 * 如果有更巧妙的方法，请面试官不吝赐教。
 */

int main()
{
    // task1(unsortedRow, sizeof(unsortedRow) / sizeof(Row));
    // task2(sortedRow, sizeof(sortedRow) / sizeof(Row));
    task3(sortedRow, sizeof(sortedRow) / sizeof(Row));
    return RETURN_OK;
}