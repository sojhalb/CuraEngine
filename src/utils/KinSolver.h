#ifndef UTILS_KINSOLVER_H
#define UTILS_KINSOLVER_H

namespace cura
{
    class KinSolver
    {
    public: 
        int theta1,theta2;
        int y1, y2;
        KinSolver(int R, int x1, int x2, int z1, int z2)
        {
            theta1 = theta2 = 1;
            y1 = y2 = 1;
        };
    };
}

#endif //UTILS_KINSOLVER_H