#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <omp.h>
using namespace std;

#define DIM 768

struct cuComplex {
    float r;
    float i;
    cuComplex(float a, float b) : r(a), i(b) {}
    float magnitude2(void) { return r * r + i * i; }
    cuComplex operator*(const cuComplex& a) {
        return cuComplex(r * a.r - i * a.i, i * a.r + r * a.i);
    }
    cuComplex operator+(const cuComplex& a) {
        return cuComplex(r + a.r, i + a.i);
    }
};

int julia(int x, int y) {
    const float scale = 1.5;
    float jx = scale * (float)(DIM / 2 - x) / (DIM / 2);
    float jy = scale * (float)(DIM / 2 - y) / (DIM / 2);
    cuComplex c(-0.7269, 0.1889);
    cuComplex a(jx, jy);
    for (int i = 0; i < 300; i++) {
        a = a * a + c;
        if (a.magnitude2() > 1000)
            return 0;
    }
    return 1;
}

/* ----------------------------------------------------------------
 * Serial baseline
 * ---------------------------------------------------------------- */
void kernel_serial(unsigned char* ptr) {
    for (int y = 0; y < DIM; y++)
        for (int x = 0; x < DIM; x++) {
            int offset = x + y * DIM;
            int v = julia(x, y);
            ptr[offset*3+0] = 255*v; ptr[offset*3+1] = 0; ptr[offset*3+2] = 0;
        }
}

/* ----------------------------------------------------------------
 * (a) 1D Rowwise Parallel
 * Thread tid handles rows: tid, tid+T, tid+2T, ...
 * Manual decomposition per Example 4.2.3 style.
 * ---------------------------------------------------------------- */
void kernel_1d_row(unsigned char* ptr) {
    int T = omp_get_max_threads();
#pragma omp parallel num_threads(T)
    {
        int tid = omp_get_thread_num();
        for (int y = tid; y < DIM; y += T)
            for (int x = 0; x < DIM; x++) {
                int offset = x + y * DIM;
                int v = julia(x, y);
                ptr[offset*3+0] = 255*v; ptr[offset*3+1] = 0; ptr[offset*3+2] = 0;
            }
    }
}

/* ----------------------------------------------------------------
 * (b) 1D Columnwise Parallel
 * Thread tid handles columns: tid, tid+T, tid+2T, ...
 * Manual decomposition per Example 4.2.3 style.
 * ---------------------------------------------------------------- */
void kernel_1d_col(unsigned char* ptr) {
    int T = omp_get_max_threads();
#pragma omp parallel num_threads(T)
    {
        int tid = omp_get_thread_num();
        for (int x = tid; x < DIM; x += T)
            for (int y = 0; y < DIM; y++) {
                int offset = x + y * DIM;
                int v = julia(x, y);
                ptr[offset*3+0] = 255*v; ptr[offset*3+1] = 0; ptr[offset*3+2] = 0;
            }
    }
}

/* ----------------------------------------------------------------
 * (c) 2D Row-Block Parallel
 * DIM rows divided into T contiguous blocks.
 * Thread tid owns rows [tid*(DIM/T) .. next_start-1].
 * Last thread absorbs any remainder rows.
 * ---------------------------------------------------------------- */
void kernel_2d_row_block(unsigned char* ptr) {
    int T = omp_get_max_threads();
#pragma omp parallel num_threads(T)
    {
        int tid = omp_get_thread_num();
        int block = DIM / T;
        int row_start = tid * block;
        int row_end   = (tid == T-1) ? DIM : row_start + block;
        for (int y = row_start; y < row_end; y++)
            for (int x = 0; x < DIM; x++) {
                int offset = x + y * DIM;
                int v = julia(x, y);
                ptr[offset*3+0] = 255*v; ptr[offset*3+1] = 0; ptr[offset*3+2] = 0;
            }
    }
}

/* ----------------------------------------------------------------
 * (d) 2D Column-Block Parallel
 * DIM columns divided into T contiguous blocks.
 * Thread tid owns columns [tid*(DIM/T) .. next_start-1].
 * Last thread absorbs any remainder columns.
 * ---------------------------------------------------------------- */
void kernel_2d_col_block(unsigned char* ptr) {
    int T = omp_get_max_threads();
#pragma omp parallel num_threads(T)
    {
        int tid = omp_get_thread_num();
        int block = DIM / T;
        int col_start = tid * block;
        int col_end   = (tid == T-1) ? DIM : col_start + block;
        for (int x = col_start; x < col_end; x++)
            for (int y = 0; y < DIM; y++) {
                int offset = x + y * DIM;
                int v = julia(x, y);
                ptr[offset*3+0] = 255*v; ptr[offset*3+1] = 0; ptr[offset*3+2] = 0;
            }
    }
}

/* ----------------------------------------------------------------
 * (e) OpenMP for construct
 * Parallelise the outer (row) loop; OpenMP handles decomposition.
 * ---------------------------------------------------------------- */
void kernel_omp_for(unsigned char* ptr) {
#pragma omp parallel for schedule(static)
    for (int y = 0; y < DIM; y++)
        for (int x = 0; x < DIM; x++) {
            int offset = x + y * DIM;
            int v = julia(x, y);
            ptr[offset*3+0] = 255*v; ptr[offset*3+1] = 0; ptr[offset*3+2] = 0;
        }
}

/* ----------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------- */
void save_ppm(const char* filename, unsigned char* data, int width, int height) {
    ofstream file(filename, ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";
    file.write(reinterpret_cast<char*>(data), width * height * 3);
    file.close();
}

bool images_equal(unsigned char* a, unsigned char* b, int size) {
    return memcmp(a, b, size) == 0;
}

/* ----------------------------------------------------------------
 * Main: benchmark all 5 implementations over all thread counts
 * Outputs CSV rows for easy plotting.
 * ---------------------------------------------------------------- */
int main(void) {
    const int IMG_SIZE = DIM * DIM * 3;
    int thread_counts[] = {1, 2, 4, 6, 8, 10, 12, 14, 16};
    int num_configs = sizeof(thread_counts) / sizeof(thread_counts[0]);

    unsigned char* image_ref  = new unsigned char[IMG_SIZE];
    unsigned char* image_test = new unsigned char[IMG_SIZE];

    // Serial reference run
    double t_start = omp_get_wtime();
    kernel_serial(image_ref);
    double t_serial = omp_get_wtime() - t_start;

    cout << "Serial time: " << t_serial << " s\n\n";
    save_ppm("output/fractal_serial.ppm", image_ref, DIM, DIM);

    const char* names[] = {
        "1D Rowwise",
        "1D Colwise",
        "2D Row-Block",
        "2D Col-Block",
        "OMP For"
    };
    typedef void (*KernelFn)(unsigned char*);
    KernelFn kernels[] = {
        kernel_1d_row,
        kernel_1d_col,
        kernel_2d_row_block,
        kernel_2d_col_block,
        kernel_omp_for
    };
    const char* save_names[] = {
        "output/fractal_1d_row.ppm",
        "output/fractal_1d_col.ppm",
        "output/fractal_2d_row_block.ppm",
        "output/fractal_2d_col_block.ppm",
        "output/fractal_omp_for.ppm"
    };
    const int NUM_KERNELS = 5;

    cout << "Kernel,Threads,Time(s),Speedup,Correct\n";

    for (int k = 0; k < NUM_KERNELS; k++) {
        for (int tc = 0; tc < num_configs; tc++) {
            int T = thread_counts[tc];
            omp_set_num_threads(T);
            memset(image_test, 0, IMG_SIZE);

            double t0 = omp_get_wtime();
            kernels[k](image_test);
            double elapsed = omp_get_wtime() - t0;

            double speedup = t_serial / elapsed;
            bool correct = images_equal(image_ref, image_test, IMG_SIZE);

            cout << names[k] << "," << T << ","
                 << elapsed << "," << speedup << ","
                 << (correct ? "YES" : "NO") << "\n";

            if (T == 16)
                save_ppm(save_names[k], image_test, DIM, DIM);
        }
    }

    delete[] image_ref;
    delete[] image_test;
    return 0;
}
