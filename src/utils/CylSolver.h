#ifndef UTILS_KINSOLVER_H
#define UTILS_KINSOLVER_H

#include <stdlib.h>
#include <math.h>

#include <kinsol/kinsol.h>             /* access to KINSOL func., consts. */
#include <nvector/nvector_serial.h>    /* access to serial N_Vector       */
#include <sunmatrix/sunmatrix_dense.h> /* access to dense SUNMatrix       */
#include <kinsol/kinsol_direct.h>      /* access to KINDls interface      */
#include <sunlinsol/sunlinsol_dense.h> /* access to dense SUNLinearSolver */
#include <sundials/sundials_types.h>   /* defs. of realtype, sunindextype */
#include <sundials/sundials_math.h>    /* access to SUNRexp               */

/* Problem Constants */

#define NVAR 2
#define NEQ 3 * (NVAR)

#define FTOL RCONST(1.e-5) /* function tolerance */
#define STOL RCONST(1.e-5) /* step tolerance */

#define PI RCONST(3.1415926)
#define ZERO RCONST(0.0)
#define ONE RCONST(1.0)
#define PT5 RCONST(0.5)
#define TWO RCONST(2.0)

typedef struct
{
    realtype lb[NVAR];
    realtype ub[NVAR];
    realtype px1[NVAR];
    realtype px2[NVAR];
    realtype pz1[NVAR];
    realtype pz2[NVAR];
    realtype R[NVAR];
} * UserData;

/* Accessor macro */
#define Ith(v, i) NV_Ith_S(v, i - 1)

/* Functions Called by the KINSOL Solver */

/* Private Helper Functions */
namespace cura
{
class CylSolver
{
  public:
    realtype theta1, theta2;
    realtype t1, t2;
    Point* itx_p1, *itx_p2, *itx_either;
    CylSolver(Point3 p1, Point3 p2, realtype R)
    {
        UserData data;
        realtype fnormtol, scsteptol;
        N_Vector u1, u2, u, s, c;
        int glstr, mset, flag;
        void *kmem;
        SUNMatrix J;
        SUNLinearSolver LS;

        u1 = u2 = u = NULL;
        s = c = NULL;
        kmem = NULL;
        J = NULL;
        LS = NULL;
        data = NULL;

        /* User data */

        data = (UserData)malloc(sizeof *data);
        // set lower and upper limits for theta to theta1 and theta2
        realtype px1 = p1.x;
        realtype pz1 = p1.z;
        realtype px2 = p2.x;
        realtype pz2 = p2.z;
        theta1 = atan2(pz1, px1);
        theta2 = atan2(pz2, px2);

        if (theta1 < theta2)
        {
            data->lb[0] = theta1;
            data->ub[0] = theta2;
        }
        else
        {
            data->lb[0] = theta2;
            data->ub[0] = theta1;
        }

        //set lower and upper limits of t, t = 0 is p1 and t = 1 is p2
        data->lb[1] = ZERO;
        data->ub[1] = 1;

        data->px1[0] = data->px1[1] = px1;
        data->px2[0] = data->px2[1] = px2;
        data->pz1[0] = data->pz1[1] = pz1;
        data->pz2[0] = data->pz2[1] = pz2;
        data->R[0] = data->R[1] = R;

        u1 = N_VNew_Serial(NEQ);
        if (check_flag((void *)u1, "N_VNew_Serial", 0))
            return;

        u2 = N_VNew_Serial(NEQ);
        if (check_flag((void *)u2, "N_VNew_Serial", 0))
            return;

        u = N_VNew_Serial(NEQ);
        if (check_flag((void *)u, "N_VNew_Serial", 0))
            return;

        s = N_VNew_Serial(NEQ);
        if (check_flag((void *)s, "N_VNew_Serial", 0))
            return;

        c = N_VNew_Serial(NEQ);
        if (check_flag((void *)c, "N_VNew_Serial", 0))
            return;

        SetInitialGuess1(u1, data);
        SetInitialGuess2(u2, data);

        N_VConst_Serial(ONE, s); /* no scaling */

        Ith(c, 1) = ZERO; /* no constraint on x1 */
        Ith(c, 2) = ZERO; /* no constraint on x2 */
        Ith(c, 3) = ONE;  /* l1 = x1 - x1_min >= 0 */
        Ith(c, 4) = -ONE; /* L1 = x1 - x1_max <= 0 */
        Ith(c, 5) = ONE;  /* l2 = x2 - x2_min >= 0 */
        Ith(c, 6) = -ONE; /* L2 = x2 - x22_min <= 0 */

        fnormtol = FTOL;
        scsteptol = STOL;

        kmem = KINCreate();
        if (check_flag((void *)kmem, "KINCreate", 0))
            return;

        flag = KINSetUserData(kmem, data);
        if (check_flag(&flag, "KINSetUserData", 1))
            return;
        flag = KINSetConstraints(kmem, c);
        if (check_flag(&flag, "KINSetConstraints", 1))
            return;
        flag = KINSetFuncNormTol(kmem, fnormtol);
        if (check_flag(&flag, "KINSetFuncNormTol", 1))
            return;
        flag = KINSetScaledStepTol(kmem, scsteptol);
        if (check_flag(&flag, "KINSetScaledStepTol", 1))
            return;

        flag = KINInit(kmem, func, u);
        if (check_flag(&flag, "KINInit", 1))
            return;

        /* Create dense SUNMatrix */
        J = SUNDenseMatrix(NEQ, NEQ);
        if (check_flag((void *)J, "SUNDenseMatrix", 0))
            return;

        /* Create dense SUNLinearSolver object */
        LS = SUNDenseLinearSolver(u, J);
        if (check_flag((void *)LS, "SUNDenseLinearSolver", 0))
            return;

        /* Attach the matrix and linear solver to KINSOL */
        flag = KINDlsSetLinearSolver(kmem, LS, J);
        if (check_flag(&flag, "KINDlsSetLinearSolver", 1))
            return;

        // printf("\n------------------------------------------\n");
        // printf("\nInitial guess at p1\n");
        // printf("  [x1,x2] = ");
        // PrintOutput(u1);

        N_VScale_Serial(ONE, u1, u);
        glstr = KIN_NONE;
        mset = 1;
        if( SolveIt(kmem, u, s, glstr, mset) )
            printf("\n error from p1 end of: ( %d, %d, %d ) to ( %d, %d, %d )", p1.x, p1.y, p1.z, p2.x, p2.y, p2.x);

        theta1 = Ith(u, 1);
        t1 = Ith(u, 2);

        // printf("\n------------------------------------------\n");
        // printf("\nInitial guess at p2 \n");
        // printf("  [x1,x2] = ");
        // PrintOutput(u2);

        N_VScale_Serial(ONE, u2, u);
        glstr = KIN_NONE;
        mset = 1;
        if ( SolveIt(kmem, u, s, glstr, mset))
            printf("\n error from p2 end of: ( %d, %d, %d ) to ( %d, %d, %d )", p1.x, p1.y, p1.z, p2.x, p2.y, p2.x);

        theta2 = Ith(u, 1);
        t2 = Ith(u, 2);

        itx_p1 = new Point(theta1, calcYFromT(p1, p2, t1));
        itx_p2 = new Point(theta2, calcYFromT(p1,p2,t2));
        itx_either = itx_p1;

    }
    CylSolver ()
    {
        
    }

    // test cases for cyl solver

    //solve for cylinder with triangle intersections
    //CylSolver* cs = new CylSolver(1,2,-1,-1,2);
    //expected solutions at: (theta = 0, t = 1/3)
    //                  and: (theta = pi/2, t = 2/3)

    //CylSolver* cs2 = new CylSolver(2,3,-4,-1,4);
    // expected solutions at: (theta = 0.644, t = 0.35)
    //                  and: (theta = pi/2, t = 0.75)

    //CylSolver* cs3 = new CylSolver(2,-1,4, 3, -4);
    // expected solutions at: (theta = 0.644, t = 0.35)
    //                  and: (theta = pi/2, t = 0.75)

    static realtype directCalcT(Point3 p1, Point3 p2, realtype theta)
    {
        realtype A = sin(theta) / cos(theta);
        realtype num = p1.x - p1.z * A;
        realtype den = A * ((p2.z - p1.z) - (p2.x - p1.x));
        return num / den;
    }

    static realtype calcYFromT(Point3 p1, Point3 p2, realtype t)
    {
        return (p1.y + (p2.y - p1.y)*t);
    }

    /*
 *--------------------------------------------------------------------
 * PRIVATE FUNCTIONS
 *--------------------------------------------------------------------
 */

    /*
 * Initial guesses
 */

    static void SetInitialGuess1(N_Vector u, UserData data)
    {
        realtype x1, x2;
        realtype *udata;
        realtype *lb, *ub;

        udata = N_VGetArrayPointer_Serial(u);

        lb = data->lb;
        ub = data->ub;

        /* There are two known solutions for this problem */
        /* this init. guess should take us to solution nearest p1 */
        x1 = lb[0];
        x2 = lb[1];

        udata[0] = x1;
        udata[1] = x2;
        udata[2] = x1 - lb[0];
        udata[3] = x1 - ub[0];
        udata[4] = x2 - lb[1];
        udata[5] = x2 - ub[1];
    }

    static void SetInitialGuess2(N_Vector u, UserData data)
    {
        realtype x1, x2;
        realtype *udata;
        realtype *lb, *ub;

        udata = N_VGetArrayPointer_Serial(u);

        lb = data->lb;
        ub = data->ub;

        /* There are two known solutions for this problem */
        /* this init. guess should take us to solution nearest p2 */
        x1 = ub[0];
        x2 = ub[1];

        udata[0] = x1;
        udata[1] = x2;
        udata[2] = x1 - lb[0];
        udata[3] = x1 - ub[0];
        udata[4] = x2 - lb[1];
        udata[5] = x2 - ub[1];
    }

    static int check_flag(void *flagvalue, const char *funcname, int opt)
    {
        int *errflag;

        /* Check if SUNDIALS function returned NULL pointer - no memory allocated */
        if (opt == 0 && flagvalue == NULL)
        {
            fprintf(stderr,
                    "\nSUNDIALS_ERROR: %s() failed - returned NULL pointer\n\n",
                    funcname);
            return (1);
        }

        /* Check if flag < 0 */
        else if (opt == 1)
        {
            errflag = (int *)flagvalue;
            if (*errflag < 0)
            {
                fprintf(stderr,
                        "\nSUNDIALS_ERROR: %s() failed with flag = %d\n\n",
                        funcname, *errflag);
                return (1);
            }
        }

        /* Check if function returned NULL pointer - no memory allocated */
        else if (opt == 2 && flagvalue == NULL)
        {
            fprintf(stderr,
                    "\nMEMORY_ERROR: %s() failed - returned NULL pointer\n\n",
                    funcname);
            return (1);
        }
        return (0);
    }

    static int SolveIt(void *kmem, N_Vector u, N_Vector s, int glstr, int mset)
    {
        int flag;

        flag = KINSetMaxSetupCalls(kmem, mset);
        if (check_flag(&flag, "KINSetMaxSetupCalls", 1))
            return (1);

        flag = KINSol(kmem, u, glstr, s, s);
        if (check_flag(&flag, "KINSol", 1))
            return (1);

        return (0);
    }

    // static int SolveIt(void *kmem, N_Vector u, N_Vector s, int glstr, int mset)
    // {
    //     int flag;

    //     printf("\n");

    //     if (mset == 1)
    //         printf("Exact Newton");
    //     else
    //         printf("Modified Newton");

    //     if (glstr == KIN_NONE)
    //         printf("\n");
    //     else
    //         printf(" with line search\n");

    //     flag = KINSetMaxSetupCalls(kmem, mset);
    //     if (check_flag(&flag, "KINSetMaxSetupCalls", 1))
    //         return (1);

    //     flag = KINSol(kmem, u, glstr, s, s);
    //     if (check_flag(&flag, "KINSol", 1))
    //         return (1);

    //     printf("Solution:\n  [x1,x2] = ");
    //     PrintOutput(u);

    //     PrintFinalStats(kmem);

    //     return (0);
    // }

    static void PrintOutput(N_Vector u)
    {
#if defined(SUNDIALS_EXTENDED_PRECISION)
        printf(" %8.6Lg  %8.6Lg\n", Ith(u, 1), Ith(u, 2));
#elif defined(SUNDIALS_DOUBLE_PRECISION)
        printf(" %8.6g  %8.6g\n", Ith(u, 1), Ith(u, 2));
#else
        printf(" %8.6g  %8.6g\n", Ith(u, 1), Ith(u, 2));
#endif
    }

    /* 
 * Print final statistics contained in iopt 
 */

    static void PrintFinalStats(void *kmem)
    {
        long int nni, nfe, nje, nfeD;
        int flag;

        flag = KINGetNumNonlinSolvIters(kmem, &nni);
        check_flag(&flag, "KINGetNumNonlinSolvIters", 1);
        flag = KINGetNumFuncEvals(kmem, &nfe);
        check_flag(&flag, "KINGetNumFuncEvals", 1);

        flag = KINDlsGetNumJacEvals(kmem, &nje);
        check_flag(&flag, "KINDlsGetNumJacEvals", 1);
        flag = KINDlsGetNumFuncEvals(kmem, &nfeD);
        check_flag(&flag, "KINDlsGetNumFuncEvals", 1);

        printf("Final Statistics:\n");
        printf("  nni = %5ld    nfe  = %5ld \n", nni, nfe);
        printf("  nje = %5ld    nfeD = %5ld \n", nje, nfeD);
    }

    static int func(N_Vector u, N_Vector f, void *user_data)
    {
        realtype *udata, *fdata;
        realtype x1, l1, L1, x2, l2, L2;
        realtype *px1, *px2, *pz1, *pz2, *R;
        realtype *lb, *ub;
        UserData data;

        data = (UserData)user_data;
        lb = data->lb;
        ub = data->ub;
        px1 = data->px1;
        px2 = data->px2;
        pz1 = data->pz1;
        pz2 = data->pz2;
        R = data->R;

        udata = N_VGetArrayPointer_Serial(u);
        fdata = N_VGetArrayPointer_Serial(f);

        x1 = udata[0];
        x2 = udata[1];
        l1 = udata[2];
        L1 = udata[3];
        l2 = udata[4];
        L2 = udata[5];

        fdata[0] = px1[0] + (px2[0] - px1[0]) * x2 - R[0] * cos(x1);
        fdata[1] = pz1[0] + (pz2[0] - pz1[0]) * x2 - R[0] * sin(x1);
        fdata[2] = l1 - x1 + lb[0];
        fdata[3] = L1 - x1 + ub[0];
        fdata[4] = l2 - x2 + lb[1];
        fdata[5] = L2 - x2 + ub[1];

        return (0);
    }

}; // class CylSolver

} // namespace cura

#endif //UTILS_KINSOLVER_H