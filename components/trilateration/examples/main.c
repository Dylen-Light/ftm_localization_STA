/*
 * Academic License - for use in teaching, academic research, and meeting
 * course requirements at degree granting institutions only.  Not for
 * government, commercial, or other organizational use.
 * File: main.c
 *
 * MATLAB Coder version            : 23.2
 * C/C++ source code generated on  : 14-Mar-2024 14:32:04
 */

/*************************************************************************/
/* This automatically generated example C main file shows how to call    */
/* entry-point functions that MATLAB Coder generated. You must customize */
/* this file for your application. Do not modify this file directly.     */
/* Instead, make a copy of this file, modify it, and integrate it into   */
/* your development environment.                                         */
/*                                                                       */
/* This file initializes entry-point function arguments to a default     */
/* size and value before calling the entry-point functions. It does      */
/* not store or use any values returned from the entry-point functions.  */
/* If necessary, it does pre-allocate memory for returned values.        */
/* You can use this file as a starting point for a main function that    */
/* you can deploy in your application.                                   */
/*                                                                       */
/* After you copy the file, and before you deploy it, you must make the  */
/* following changes:                                                    */
/* * For variable-size function arguments, change the example sizes to   */
/* the sizes that your application requires.                             */
/* * Change the example values of function arguments to the values that  */
/* your application requires.                                            */
/* * If the entry-point functions return values, store these values or   */
/* otherwise use them as required by your application.                   */
/*                                                                       */
/*************************************************************************/

/* Include Files */
#include "main.h"
#include "trilateration.h"
#include "trilateration_terminate.h"

/* Function Declarations */
static void argInit_1xd3_real_T(double result_data[], int result_size[2]);

static void argInit_d3x2_real_T(double result_data[], int result_size[2]);

static double argInit_real_T(void);

/* Function Definitions */
/*
 * Arguments    : double result_data[]
 *                int result_size[2]
 * Return Type  : void
 */
static void argInit_1xd3_real_T(double result_data[], int result_size[2])
{
  int idx1;
  /* Set the size of the array.
Change this size to the value that the application requires. */
  result_size[0] = 1;
  result_size[1] = 2;
  /* Loop over the array to initialize each element. */
  for (idx1 = 0; idx1 < 2; idx1++) {
    /* Set the value of the array element.
Change this value to the value that the application requires. */
    result_data[idx1] = argInit_real_T();
  }
}

/*
 * Arguments    : double result_data[]
 *                int result_size[2]
 * Return Type  : void
 */
static void argInit_d3x2_real_T(double result_data[], int result_size[2])
{
  int i;
  /* Set the size of the array.
Change this size to the value that the application requires. */
  result_size[0] = 2;
  result_size[1] = 2;
  /* Loop over the array to initialize each element. */
  for (i = 0; i < 4; i++) {
    /* Set the value of the array element.
Change this value to the value that the application requires. */
    result_data[i] = argInit_real_T();
  }
}

/*
 * Arguments    : void
 * Return Type  : double
 */
static double argInit_real_T(void)
{
  return 0.0;
}

/*
 * Arguments    : int argc
 *                char **argv
 * Return Type  : int
 */
int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  /* The initialize function is being called automatically from your entry-point
   * function. So, a call to initialize is not included here. */
  /* Invoke the entry-point functions.
You can call entry-point functions multiple times. */
  main_trilateration();
  /* Terminate the application.
You do not need to do this more than one time. */
  trilateration_terminate();
  return 0;
}

/*
 * Arguments    : void
 * Return Type  : void
 */
void main_trilateration(void)
{
  double nodeList_data[6];
  double disList_data[3];
  double X[2];
  int disList_size[2];
  int nodeList_size[2];
  /* Initialize function 'trilateration' input arguments. */
  /* Initialize function input argument 'nodeList'. */
  argInit_d3x2_real_T(nodeList_data, nodeList_size);
  /* Initialize function input argument 'disList'. */
  argInit_1xd3_real_T(disList_data, disList_size);
  /* Call the entry-point 'trilateration'. */
  trilateration(argInit_real_T(), nodeList_data, nodeList_size, disList_data,
                disList_size, X);
}

/*
 * File trailer for main.c
 *
 * [EOF]
 */
