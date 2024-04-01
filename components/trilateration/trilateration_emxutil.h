/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 * File: trilateration_emxutil.h
 *
 * MATLAB Coder version            : 23.2
 * C/C++ source code generated on  : 14-Mar-2024 14:32:04
 */

#ifndef TRILATERATION_EMXUTIL_H
#define TRILATERATION_EMXUTIL_H

/* Include Files */
#include "rtwtypes.h"
#include "trilateration_types.h"
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Function Declarations */
extern void emxEnsureCapacity_real_T(emxArray_real_T *emxArray, int oldNumel);

extern void emxFree_real_T(emxArray_real_T **pEmxArray);

extern void emxInit_real_T(emxArray_real_T **pEmxArray, int numDimensions);

#ifdef __cplusplus
}
#endif

#endif
/*
 * File trailer for trilateration_emxutil.h
 *
 * [EOF]
 */
