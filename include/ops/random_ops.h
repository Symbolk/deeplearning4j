//
// @author raver119@gmail.com
//

#ifndef LIBND4J_RANDOM_OPS_H
#define LIBND4J_RANDOM_OPS_H

#ifdef __CUDACC__
#define random_def __device__ inline static
#else
#define random_def inline static
#endif

// since we can't inherit/overwrite static methods - we just define default impls
#define method_idx  random_def T op(int idx, int length, nd4j::random::RandomHelper<T> *helper, T *extraParams) { return -1.0f; }
#define method_X  random_def T op(T valueX, int idx, int length, nd4j::random::RandomHelper<T> *helper, T *extraParams) { return -2.0f; }
#define method_XY  random_def T op(T valueX, T valueY, int idx, int length, nd4j::random::RandomHelper<T> *helper, T *extraParams) { return -3.0f; }

#define no_exec_special static const bool requiresSpecial = false; static inline void specialOp(Nd4jPointer state, T *x, int *xShapeBuffer, T *y, int *yShapeBuffer, T *z, int *zShapeBuffer, T *extraArguments) { }

#include <helpers/helper_random.h>

namespace randomOps {

    /**
     * This Op merges two arrays per-element, if probability meets threshold
     */
    template<typename T>
    class ProbablisticMerge {
    public:

        no_exec_special

        method_idx
        method_X

        random_def T op(T valueX, T valueY, int idx,  int length, nd4j::random::RandomHelper<T> *helper, T *extraParams) {
            T threshold = extraParams[0];
            T randVal = helper->relativeT(idx);

            return randVal <= threshold ? valueY : valueX;
        }
    };

    /**
     * This Op produces random values within specified boundaries. Disribution is uniform
     */
    template<typename T>
    class UniformDistribution {
    public:

        no_exec_special

        method_XY
        method_X

        random_def T op(int idx, int length, nd4j::random::RandomHelper<T> *helper, T *extraParams) {
            return helper->relativeT(idx, extraParams[0], extraParams[1]);
        }
    };


    /**
     * Basic DropOut/DropConnect Op
     */
    template<typename T>
    class DropOut {
    public:

        no_exec_special

        method_idx
        method_XY

        random_def T op(T valueX, int idx, int length, nd4j::random::RandomHelper<T> *helper, T *extraParams) {
            T randVal = helper->relativeT(idx);
            return randVal <= extraParams[0] ? (T) 0.0f : valueX;
        }
    };

    /**
     * Inverted DropOut implementation, used in DL4j
     */
    template<typename T>
    class DropOutInverted {
    public:

        no_exec_special

        method_idx
        method_XY

        random_def T op(T valueX, int idx, int length, nd4j::random::RandomHelper<T> *helper, T *extraParams) {
            T prob = extraParams[0];
            T randVal = helper->relativeT(idx);
            return randVal >= prob ? (T) 0.0f : valueX / prob;
        }
    };


    template<typename T>
    class Linspace {
    public:

        no_exec_special

        method_X
        method_XY

        random_def T op(int idx, int length, nd4j::random::RandomHelper<T> *helper, T *extraParams) {
            T from = extraParams[0];
            T to = extraParams[1];

            T step = (T) idx / ((T)length - (T) 1.0f);

            return from * ((T) 1.0f - step) + step * to;
        }
    };
}

#endif //LIBND4J_RANDOM_OPS_H
