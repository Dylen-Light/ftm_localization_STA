/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 * File: trilateration.h
 *
 * MATLAB Coder version            : 23.2
 * C/C++ source code generated on  : 14-Mar-2024 14:32:04
 */

#ifndef TRILATERATION_H
#define TRILATERATION_H

/* Include Files */
#include "rtwtypes.h"
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Function Declarations */
extern void trilateration(double nodeNumber, const double nodeList_data[],
                          const int nodeList_size[2],
                          const double disList_data[],
                          const int disList_size[2], double X[2]);

#ifdef __cplusplus
}
#endif

#endif
/*
 * File trailer for trilateration.h
 *
 * [EOF]
 */
