/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 * File: trilateration.c
 *
 * MATLAB Coder version            : 23.2
 * C/C++ source code generated on  : 14-Mar-2024 14:32:04
 */

/* Include Files */
#include "trilateration.h"
#include "trilateration_emxutil.h"
#include "trilateration_types.h"
#include <math.h>

/* Function Declarations */
static double rt_roundd_snf(double u);

/* Function Definitions */
/*
 * Arguments    : double u
 * Return Type  : double
 */
static double rt_roundd_snf(double u)
{
  double y;
  if (fabs(u) < 4.503599627370496E+15) {
    if (u >= 0.5) {
      y = floor(u + 0.5);
    } else if (u > -0.5) {
      y = u * 0.0;
    } else {
      y = ceil(u - 0.5);
    }
  } else {
    y = u;
  }
  return y;
}

/*
 * Arguments    : double nodeNumber
 *                const double nodeList_data[]
 *                const int nodeList_size[2]
 *                const double disList_data[]
 *                const int disList_size[2]
 *                double X[2]
 * Return Type  : void
 */
void trilateration(double nodeNumber, const double nodeList_data[],
                   const int nodeList_size[2], const double disList_data[],
                   const int disList_size[2], double X[2])
{
  emxArray_real_T *A;
  emxArray_real_T *B;
  emxArray_real_T *b_A;
  emxArray_real_T *b_y;
  double out[6];
  double y[4];
  double b_out[3];
  double B_tmp;
  double b_B_tmp;
  double bkj;
  double t;
  double yn_tmp;
  double *A_data;
  double *B_data;
  double *y_data;
  int boffset;
  int coffset;
  int inner;
  int j;
  int k;
  unsigned char b_nodeNumber;
  unsigned char i;
  bkj = rt_roundd_snf(nodeNumber);
  if (bkj < 256.0) {
    if (bkj >= 0.0) {
      b_nodeNumber = (unsigned char)bkj;
    } else {
      b_nodeNumber = 0U;
    }
  } else if (bkj >= 256.0) {
    b_nodeNumber = MAX_uint8_T;
  } else {
    b_nodeNumber = 0U;
  }
  if (nodeList_size[0] != 0) {
    for (coffset = 0; coffset < 6; coffset++) {
      out[coffset] = nodeList_data[coffset];
    }
  }
  if (disList_size[1] == 1) {
    b_out[0] = disList_data[0];
    b_out[1] = disList_data[0];
    b_out[2] = disList_data[0];
  } else if (disList_size[1] != 0) {
    b_out[0] = disList_data[0];
    b_out[1] = disList_data[1];
    b_out[2] = disList_data[2];
  }
  /*  nodeNumber = 3;   %定位信标的数量 */
  /*  nodeList = [0, 0; 2, 0; 1, 1.732];   %三个定位信标的坐标 */
  /*  disList = [1.155, 1.155, 1.155];    %定位目标点到三个定位信标的距离 */
  t = out[b_nodeNumber - 1];
  yn_tmp = out[b_nodeNumber + 2];
  bkj = b_out[b_nodeNumber - 1];
  emxInit_real_T(&A, 2);
  coffset = A->size[0] * A->size[1];
  A->size[0] = 1;
  A->size[1] = 2;
  emxEnsureCapacity_real_T(A, coffset);
  A_data = A->data;
  A_data[0] = 2.0 * (out[0] - t);
  A_data[1] = 2.0 * (out[3] - yn_tmp);
  emxInit_real_T(&B, 1);
  coffset = B->size[0];
  B->size[0] = 1;
  emxEnsureCapacity_real_T(B, coffset);
  B_data = B->data;
  B_tmp = t * t;
  b_B_tmp = yn_tmp * yn_tmp;
  bkj *= bkj;
  B_data[0] =
      ((((out[0] * out[0] + out[3] * out[3]) - B_tmp) - b_B_tmp) + bkj) -
      b_out[0] * b_out[0];
  b_nodeNumber--;
  emxInit_real_T(&b_A, 2);
  for (i = 2; i <= b_nodeNumber; i++) {
    boffset = A->size[0];
    coffset = b_A->size[0] * b_A->size[1];
    b_A->size[0] = A->size[0] + 1;
    b_A->size[1] = 2;
    emxEnsureCapacity_real_T(b_A, coffset);
    B_data = b_A->data;
    for (coffset = 0; coffset < 2; coffset++) {
      for (inner = 0; inner < boffset; inner++) {
        B_data[inner + b_A->size[0] * coffset] =
            A_data[inner + A->size[0] * coffset];
      }
    }
    B_data[A->size[0]] = 2.0 * (out[1] - t);
    B_data[A->size[0] + b_A->size[0]] = 2.0 * (out[4] - yn_tmp);
    coffset = A->size[0] * A->size[1];
    A->size[0] = b_A->size[0];
    A->size[1] = 2;
    emxEnsureCapacity_real_T(A, coffset);
    A_data = A->data;
    boffset = b_A->size[0] << 1;
    for (coffset = 0; coffset < boffset; coffset++) {
      A_data[coffset] = B_data[coffset];
    }
    coffset = B->size[0];
    inner = B->size[0];
    B->size[0]++;
    emxEnsureCapacity_real_T(B, inner);
    B_data = B->data;
    B_data[coffset] =
        ((((out[1] * out[1] + out[4] * out[4]) - B_tmp) - b_B_tmp) + bkj) -
        b_out[1] * b_out[1];
  }
  emxFree_real_T(&b_A);
  /* 计算线性方程组的参数A和B */
  inner = A->size[0];
  for (j = 0; j < 2; j++) {
    coffset = j << 1;
    boffset = j * A->size[0];
    y[coffset] = 0.0;
    y[coffset + 1] = 0.0;
    for (k = 0; k < inner; k++) {
      bkj = A_data[boffset + k];
      y[coffset] += A_data[k] * bkj;
      y[coffset + 1] += A_data[A->size[0] + k] * bkj;
    }
  }
  if (fabs(y[1]) > fabs(y[0])) {
    bkj = y[0] / y[1];
    t = 1.0 / (bkj * y[3] - y[2]);
    yn_tmp = y[3] / y[1] * t;
    B_tmp = -t;
    b_B_tmp = -y[2] / y[1] * t;
    t *= bkj;
  } else {
    bkj = y[1] / y[0];
    t = 1.0 / (y[3] - bkj * y[2]);
    yn_tmp = y[3] / y[0] * t;
    B_tmp = -bkj * t;
    b_B_tmp = -y[2] / y[0] * t;
  }
  boffset = A->size[0];
  emxInit_real_T(&b_y, 2);
  coffset = b_y->size[0] * b_y->size[1];
  b_y->size[0] = 2;
  b_y->size[1] = A->size[0];
  emxEnsureCapacity_real_T(b_y, coffset);
  y_data = b_y->data;
  for (j = 0; j < boffset; j++) {
    coffset = j << 1;
    bkj = A_data[A->size[0] + j];
    y_data[coffset] = yn_tmp * A_data[j] + b_B_tmp * bkj;
    y_data[coffset + 1] = B_tmp * A_data[j] + t * bkj;
  }
  emxFree_real_T(&A);
  inner = b_y->size[1];
  X[0] = 0.0;
  X[1] = 0.0;
  for (k = 0; k < inner; k++) {
    boffset = k << 1;
    X[0] += y_data[boffset] * B_data[k];
    X[1] += y_data[boffset + 1] * B_data[k];
  }
  emxFree_real_T(&b_y);
  emxFree_real_T(&B);
  /* 根据最小二乘法公式计算结果X */
}

/*
 * File trailer for trilateration.c
 *
 * [EOF]
 */
