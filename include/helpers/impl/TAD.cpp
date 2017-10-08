//
// @author Adam Gibson
//


#include "TAD.h"
#include <pointercast.h>

namespace shape {
#ifdef __CUDACC__
    __host__ __device__
#endif
    TAD::TAD(int tadIndex,int *shapeInfo,int *dimension,int dimensionLength) {
        this->tadIndex = tadIndex;
        this->init(shapeInfo, dimension, dimensionLength);
    }

#ifdef __CUDACC__
    __host__ __device__
#endif
    TAD::TAD(int *shapeInfo,int *dimension,int dimensionLength) {
        this->init(shapeInfo, dimension, dimensionLength);
    }

    void TAD::setExternalBuffers(void *ptrManager) {
        this->ptrManager = ptrManager;
    }

    void TAD::setOutputBuffer(int *ptrOutput) {
        this->ptrOutput = ptrOutput;
    }

    void TAD::initWithExternalTAD(int *existingTAD, int *originalShape, int *dimension, int dimensionLength) {
        this->tadOnlyShapeInfo = existingTAD;
        this->rank = shape::rank(originalShape);

        this->originalShapeInfo = originalShape;
        this->originalDimension = dimension;
        this->originalDimensionLength = dimensionLength;

        this->shapeInfo = originalShape;
        this->dimension = dimension;
        this->dimensionLength = dimensionLength;

        this->tadShape = shape::shapeOf(existingTAD);
        this->tadStride = shape::stride(existingTAD);

        int ews = shape::elementWiseStride(originalShape);

        this->numTads = shape::length(originalShape) / shape::length(existingTAD); // this->tensorsAlongDimension(this->shapeInfo, this->dimension, this->dimensionLength);//shape::length(originalShape) / shape::length(existingTAD);
        this->wholeThing = this->numTads == 1 || ((this->dimensionLength == this->rank || this->numTads == shape::length(this->shapeInfo)) && ews == 1);
    }

    void TAD::init(int *shapeInfo,int *dimension,int dimensionLength) {
        this->originalShapeInfo = shapeInfo;
        this->originalDimension = dimension;
        this->originalDimensionLength = dimensionLength;
        //start off as original references
        this->shapeInfo = shapeInfo;
        this->dimensionLength = dimensionLength;
        this->dimension = dimension;
        this->rank = shape::rank(shapeInfo);
        this->numTads = this->tensorsAlongDimension(this->shapeInfo, this->dimension, this->dimensionLength);

        int ews = shape::elementWiseStride(shapeInfo);

        if(!shape::isVector(shapeInfo))
            wholeThing = this->numTads == 1 || ((this->dimensionLength == this->rank || this->numTads == shape::length(shapeInfo)) && ews == 1);
        else if(shape::isScalar(shapeInfo))
            wholeThing = true;
            //vector case
        else {
            if(dimension == 0 && shape::shapeOf(shapeInfo)[dimension[0]] == 1) {
                wholeThing = true;
            }
        }
    }

    template <typename T>
    void TAD::printTADsND(T *x) {
        if(wholeThing) {
            for(int i = 0; i < shape::length(tadOnlyShapeInfo); i++) {
                printf(" %f ",x[i]);
            }
            printf("\n");
        }
        else {
            for (int i = 0; i <  numTads; i++) {
                int offset = tadOffsets[i];
                int shapeIter[MAX_RANK];
                int coord[MAX_RANK];
                int dim;
                int rankIter = shape::rank(tadOnlyShapeInfo);
                int xStridesIter[MAX_RANK];
                T *xPointer = x + offset;
                if (PrepareOneRawArrayIter<T>(rankIter,
                                              shape::shapeOf(tadOnlyShapeInfo),
                                              xPointer,
                                              shape::stride(tadOnlyShapeInfo),
                                              &rankIter,
                                              shapeIter,
                                              &xPointer,
                                              xStridesIter) >= 0) {
                    ND4J_RAW_ITER_START(dim, shape::rank(tadOnlyShapeInfo), coord, shapeIter); {
                            /* Process the innermost dimension */
                            printf(" %f ",xPointer[0]);
                        }
                    ND4J_RAW_ITER_ONE_NEXT(dim,
                                           rankIter,
                                           coord,
                                           shapeIter,
                                           xPointer,
                                           xStridesIter);
                    printf("\n");

                }
                else {
                    printf("Unable to prepare array\n");
                }
            }
        }
    }


    void TAD::permuteShapeBufferInPlace(int *shapeBuffer,int *rearrange,int *out) {
        memcpy(out,shapeBuffer,sizeof(int) * shape::shapeInfoLength(this->rank));
        doPermuteShapeBuffer(this->rank,out,rearrange);
    }

    int* TAD::permuteShapeBuffer(int *shapeBuffer,int *rearrange) {
        int len = shape::shapeInfoLength(this->rank);
        int *copy = shape::copyOf(len,shapeBuffer);
        doPermuteShapeBuffer(rank,copy,rearrange);
        return copy;
    }

    void TAD::createTadOnlyShapeInfo() {
        this->tadOnlyShapeInfo = this->shapeInfoOnlyShapeAndStride();

        if (this->tadShape != nullptr)
            delete[] this->tadShape;

        this->tadShape = shape::shapeOf(this->tadOnlyShapeInfo);
        this->tadStride = shape::stride(this->tadOnlyShapeInfo);
        /* if(tadIndex > 0) {
             this->createOffsets();
             this->tadOnlyShapeInfo[shape::shapeInfoLength(shape::rank(this->tadOnlyShapeInfo)) - 3] = this->tadOffsets[tadIndex];
         }*/
    }

    int TAD::lengthPerSlice(int *shapeBuffer) {
        int dimension = 0;
        int *remove = shape::removeIndex(shape::shapeOf(shapeBuffer),&dimension,shape::rank(shapeBuffer),1);
        int prod = shape::prod(remove,shape::rank(shapeBuffer) - 1);
        delete[] remove;
        return prod;
    }


    int * TAD::tad2Sub(int index) {
        int *shape = shape::shapeOf(shapeInfo);
        int rank = shape::rank(shapeInfo);
        int leftOverIndexLen = rank - originalDimensionLength;
#ifdef __CUDACC__
        int *ret;
        int *tadShape;
        int *leftOverIndexes;
        int *sub;
        if (ptrManager != nullptr) {
            UnifiedSharedMemory *manager = (UnifiedSharedMemory *) ptrManager;
            ret = manager->getTempRankBuffer1();
            tadShape = manager->getTempRankBuffer2();
            leftOverIndexes = manager->getTempRankBuffer3();
            sub = manager->getTempRankBuffer4();
        } else {
            ret = new int[rank];
            tadShape = new int[leftOverIndexLen];
            leftOverIndexes = new int[leftOverIndexLen];
            sub = new int[rank];
        }
#else
        int *ret = new int[rank];
        //shape of the tad
        int *tadShape = new int[leftOverIndexLen];
        int *leftOverIndexes = new int[leftOverIndexLen];
        int *sub = new int[rank];
#endif

        //indexes not specified in the tad indexes

        //every coordinate starts as zero
        memset(ret,0,sizeof(int) * rank);

        //find the length of the elements we
        //are iterating over
        int len = 1;
        //left over index cursor for initializing elements
        int leftOverIndex = 0;
        for(int i = 0; i < rank; i++) {
            //look for dimensions NOT found in dimension length (basically compute shape - dimension (set difference)
            bool found = false;
            for(int j = 0; j < originalDimensionLength; j++) {
                //skip over specified dimensions when computing left over length
                if(i == originalDimension[j]) {
                    found = true;
                    break;
                }

            }

            //add to the indexes that aren't specified as part of the tad dimension
            //indexes
            if(!found) {
                //accumulate the list of indexes left over used for initializing the return value
                leftOverIndexes[leftOverIndex] = i;
                //accumulate the tad shape
                tadShape[leftOverIndex] = shape[i];
                //accumulate the length (product) of the indexes that will be iterated over
                len *= shape[i];
                leftOverIndex++;

            }
        }


        //sub for indices
        /* int *sub = new int[leftOverIndexLen];
         shape::ind2subOrder(tadShape,index,len,sub);
        */
        shape::ind2subC(leftOverIndexLen,tadShape,index,len, sub);


        for(int i = 0; i < leftOverIndexLen; i++) {
            ret[leftOverIndexes[i]] = sub[i];
        }

        if (ptrManager == nullptr) {
            delete[] tadShape;
            delete[] leftOverIndexes;
            delete[] sub;
        }

        return  ret;
    }


    TAD::~TAD() {
        //we may have just moved the pointer forward, we may not need to delete the pointer here
        if(originalDimension != this->dimension && createdNewDimension) {
            delete[] this->dimension;
        }
        if(this->originalShapeInfo != this->shapeInfo) {
            delete[] this->shapeInfo;
        }
        if(this->tadOffsets != nullptr) {
            delete[] this->tadOffsets;
        }

        if(this->tadOnlyShapeInfo != nullptr && this->tadOnlyShapeInfo != shapeInfo) {
            delete[] this->tadOnlyShapeInfo;
        }
    }

    int* TAD::permuteDims() {
        //permute dimensions for tad
        int dimIdx = 0;
        //loop backwards assuming dimension is sorted

        int *permuteDims = new int[shape::rank(shapeInfo)];

        for(int i = 0; i < shape::rank(shapeInfo); i++) {
            bool found = false;
            for(int j = 0; j < originalDimensionLength; j++) {
                if(i == originalDimension[j]) {
                    found = true;
                    break;
                }


            }

            //not found, append it to the end for permute
            if(!found)
                permuteDims[dimIdx++] = i;
        }



        for(int i = originalDimensionLength - 1; i >= 0; i--) {
            permuteDims[dimIdx++] = originalDimension[i];
        }

/*
            for (int i = 0; i < originalDimensionLength; i++) {
                permuteDims[i] = originalDimension[i];
            }
*/

        //permute dimensions for tad
        return permuteDims;
    }


    Nd4jIndex TAD::tadOffset(int index) {
        if(tadOnlyShapeInfo == nullptr) {
            this->createTadOnlyShapeInfo();
        }

        if(wholeThing)
            return index;

        if(dimensionLength > 1) {
            int *tad2Sub = this->tad2Sub(index,ptrManager);

            Nd4jIndex ret = shape::getOffset(0,shape::shapeOf(shapeInfo),shape::stride(shapeInfo),tad2Sub,shape::rank(shapeInfo));

            if(ret < 0) {
                if (ptrManager == nullptr)
                    delete[] tad2Sub;
                return -1;
            }
            if (ptrManager == nullptr)
                delete[] tad2Sub;

            return ret;

        }
        else {
            int *tad2Sub = this->tad2Sub(index,ptrManager);

            Nd4jIndex ret = shape::getOffset(0,shape::shapeOf(shapeInfo),shape::stride(shapeInfo),tad2Sub,shape::rank(shapeInfo));

            if (ptrManager == nullptr)
                delete[] tad2Sub;

            return ret;
        }
    }


    int* TAD::tensorShape() {
        if(this->tadShape != nullptr)
            return this->tadShape;
        int *theShape = shape::shapeOf(shapeInfo);
        int *tensorShape = shape::keep(theShape,dimension,dimensionLength,shape::rank(shapeInfo));
        this->tadShape = tensorShape;
        this->tadRank = dimensionLength;
        return tensorShape;
    }

    int * TAD::tad2Sub(int index, void *ptrManager) {
        int *shape = shape::shapeOf(shapeInfo);
        int rank = shape::rank(shapeInfo);
        int leftOverIndexLen = rank - originalDimensionLength;
        int *tadShape;
        int *leftOverIndexes;
        int *sub;
        int *ret;

#ifdef __CUDACC__

        if (ptrManager != nullptr) {
                UnifiedSharedMemory *manager = (UnifiedSharedMemory *) ptrManager;
                ret = manager->getTempRankBuffer1();
                tadShape = manager->getTempRankBuffer2();
                leftOverIndexes = manager->getTempRankBuffer3();
                sub = manager->getTempRankBuffer4();
            } else {
                ret = new int[rank];
                //shape of the tad
                leftOverIndexes = new int[leftOverIndexLen];
                sub = new int[rank];
                tadShape = new int[leftOverIndexLen];
            }
#else
        ret = new int[rank];
        //shape of the tad
        leftOverIndexes = new int[leftOverIndexLen];
        sub = new int[rank];
        tadShape = new int[leftOverIndexLen];
#endif

        //indexes not specified in the tad indexes

        //every coordinate starts as zero
        memset(ret,0,sizeof(int) * rank);


        //find the length of the elements we
        //are iterating over
        int len = 1;
        //left over index cursor for initializing elements
        int leftOverIndex = 0;
        for(int i = 0; i < rank; i++) {
            //look for dimensions NOT found in dimension length (basically compute shape - dimension (set difference)
            bool found = false;
            for(int j = 0; j < originalDimensionLength; j++) {
                //skip over specified dimensions when computing left over length
                if(i == originalDimension[j])  {
                    found = true;
                    break;
                }

            }

            //add to the indexes that aren't specified as part of the tad dimension
            //indexes
            if(!found) {
                //accumulate the list of indexes left over used for initializing the return value
                leftOverIndexes[leftOverIndex] = i;
                //accumulate the tad shape
                tadShape[leftOverIndex] = shape[i];
                //accumulate the length (product) of the indexes that will be iterated over
                leftOverIndex++;
                len *= shape[i];

            }
        }


        //sub for indices
        /* int *sub = new int[leftOverIndexLen];
         shape::ind2subOrder(tadShape,index,len,sub);
        */
        shape::ind2subC(leftOverIndexLen,tadShape,index,len, sub);

        for(int i = 0; i < leftOverIndexLen; i++) {
            ret[leftOverIndexes[i]] = sub[i];
        }

        if (ptrManager == nullptr) {
            delete[] leftOverIndexes;
            delete[] tadShape;
            delete[] sub;
        }

        return  ret;
    }

    void TAD::createOffsets() {
        this->tadOffsets = new Nd4jIndex[this->numTads];
#pragma omp parallel for schedule(guided) proc_bind(close) default(shared)
        for(int i = 0; i < this->numTads; i++) {
            this->tadOffsets[i] = this->tadOffset(i);

        }
    }


    int* TAD::shapeInfoOnlyShapeAndStride() {
        if(wholeThing && dimensionLength == 1 && dimension[0] == MAX_DIMENSION) {
            return shape::createScalarShapeInfo();
        }
        //ensure tad shapes get setup right for vectors
        if(dimensionLength < 1 && !shape::isVector(shapeInfo)) {
            return shape::copyOf(shape::shapeInfoLength(shape::rank(shapeInfo)),shapeInfo);
        }

        int *theShape = shape::shapeOf(shapeInfo);
        int rank = shape::rank(shapeInfo);

        if(dimensionLength == 1) {
            if(dimension[0] == 0 && shape::isVector(shapeInfo) && theShape[1] == 1) {
                int permuted[2] = {1,0};
                int *permutedRet2 = shape::permuteShapeBuffer(shapeInfo,permuted);
                return permutedRet2;
            } else if(dimension[0] == 1 && shape::isVector(shapeInfo) && theShape[0] == 1) {
                return shape::copyOf(shape::shapeInfoLength(shape::rank(shapeInfo)),shapeInfo);
            }
            else if(shape::shapeOf(shapeInfo)[dimension[0]] == 1) {
                int *scalarInfo = shape::createScalarShapeInfo();
                scalarInfo[shape::shapeInfoLength(shape::rank(scalarInfo)) - 3] = this->tadIndex;
                return scalarInfo;
            }
        }

        int *tensorShape = this->tensorShape();
        int *reverseDimensions = shape::reverseCopy(dimension,dimensionLength);
        int *rankRange = shape::range(0,rank);
        int *remove  = shape::removeIndex(rankRange,dimension,rank,dimensionLength);
        //concat is wrong here with the length
        int *newPermuteDims = shape::concat(remove,rank - dimensionLength,reverseDimensions,dimensionLength);
        int *permuted = shape::permuteShapeBuffer(shapeInfo,newPermuteDims);


        int sliceIndex = shape::sliceOffsetForTensor(shape::rank(permuted),
                                                     this->tadIndex,
                                                     shape::shapeOf(shapeInfo),
                                                     tensorShape,
                                                     dimensionLength,
                                                     dimension,
                                                     dimensionLength);



        int *ret2 = shape::sliceOfShapeBuffer(sliceIndex,permuted);
        int tensorLength = shape::prod(tensorShape,tadRank);

        int compLength = shape::isVector(ret2) ? shape::length(ret2)  : shape::prod(tensorShape,tadRank);
        if(dimensionLength == tadRank && compLength == shape::length(ret2)) {
            if(dimensionLength == 1 && shape::isVector(ret2) && shape::shapeOf(ret2)[0] == 1) {
                //go to the bottom and return ret2 after proper freeing of pointers
                //basic idea; we *don't* permute row vectors
            }
            else if(dimensionLength > 1) {
                //permute *then* return ret2
                int *finalPermuteDims = new int[shape::rank(ret2)];
                int forward = 0;
                for(int i = shape::rank(ret2) - 1; i >= 0; i--) {
                    finalPermuteDims[forward++] = i;
                }
                shape::permuteShapeBufferInPlace(ret2,finalPermuteDims,ret2);
                delete[] finalPermuteDims;

            }

        }
        else {
            int length = tensorLength;
            int lengthPerSlice = this->lengthPerSlice(ret2);
            int offset = tadIndex * tensorLength /lengthPerSlice;
            if(sliceIndex == 0 && length == lengthPerSlice) {
                int *newRet2 = shape::sliceOfShapeBuffer(offset,ret2);
                delete[] ret2;
                ret2 = newRet2;
                int *finalPermuteDims = new int[shape::rank(ret2)];
                int forward = 0;
                for(int i = shape::rank(ret2) - 1; i >= 0; i--) {
                    finalPermuteDims[forward++] = i;
                }
                bool isRowVector2 = shape::isRowVector(ret2);
                if(isRowVector2 == false) {
                    shape::permuteShapeBufferInPlace(ret2, finalPermuteDims, ret2);
                }

                delete[] finalPermuteDims;

            }
            else if(length == lengthPerSlice) {
                offset -= shape::slices(ret2) * (offset / shape::slices(ret2));
                int *newRet2 = shape::sliceOfShapeBuffer(offset,ret2);
                delete[] ret2;
                ret2 = newRet2;
                if(dimensionLength == 1 && shape::isVector(ret2) && shape::shapeOf(ret2)[0] == 1) {
                    //go to the bottom and return ret2 after proper freeing of pointers
                    //basic idea; we *don't* permute row vectors
                }
                else {
                    int *finalPermuteDims = new int[shape::rank(ret2)];
                    int forward = 0;
                    for(int i = shape::rank(ret2) - 1; i >= 0; i--) {
                        finalPermuteDims[forward++] = i;
                    }
                    int *newRet = shape::permuteShapeBuffer(ret2,finalPermuteDims);
                    delete[] ret2;
                    delete[] finalPermuteDims;
                    ret2 = newRet;

                }

            }
            else {
                //execute final part, note that this is mainly so delete[] gets called
                //at the bottom of the method
                while(shape::length(ret2) > length) {
                    int lengthPerSlice2 = this->lengthPerSlice(ret2);
                    sliceIndex =    sliceOffsetForTensor(sliceIndex,shape::length(ret2),lengthPerSlice2);
                    sliceIndex -= shape::slices(ret2) * (sliceIndex / shape::slices(ret2));
                    int *newRet2 = shape::sliceOfShapeBuffer(sliceIndex,ret2);
                    delete[] ret2;
                    ret2 = newRet2;
                }

                //don't permute on a row vector
                if(dimensionLength == 1 &&  shape::isVector(ret2) && shape::shapeOf(ret2)[0] == 1) {
                    //go to the bottom and return ret2 after proper freeing of pointers
                    //basic idea; we *don't* permute row vectors
                }
                else if(dimensionLength > 1){
                    //permute *then* return ret
                    int *finalPermuteDims = new int[shape::rank(ret2)];
                    int forward = 0;
                    for(int i = shape::rank(ret2) - 1; i >= 0; i--) {
                        finalPermuteDims[forward++] = i;
                    }
                    int *newPermute = shape::permuteShapeBuffer(ret2,finalPermuteDims);
                    delete[] ret2;
                    delete[] finalPermuteDims;
                    ret2 = newPermute;
                }

            }
        }


        delete[] permuted;
        delete[] newPermuteDims;
        delete[] rankRange;
        delete[] remove;
        delete[] reverseDimensions;
        return ret2;
    }


    int TAD::tadLength(int *shapeInfo, int *dimension, int dimensionLength) {
        if(dimensionLength == 1) {
            return shape::shapeOf(shapeInfo)[dimension[0]];
        }
        else {
            int ret = 1;
            for(int i = 0; i < shape::rank(shapeInfo); i++) {
                for(int j = 0; j < dimensionLength; j++) {
                    if(i == dimension[j])
                        ret *= shape::shapeOf(shapeInfo)[dimension[j]];
                }
            }
            return ret;
        }
    }


    int TAD::tensorsAlongDimension(int *shapeInfo, int *dimension, int dimensionLength) {
        return shape::length(shapeInfo) / this->tadLength(shapeInfo,dimension,dimensionLength);
    }


    void TAD::collapse() {
        int *shape = shape::shapeOf(shapeInfo);
        //handle negative dimensions/backwards indexing
        for(int i = 0; i < dimensionLength; i++) {
            if((dimension)[i] < 0)
                (dimension)[i] += shape::rank(this->shapeInfo);
        }

        this->dimension =  new int[dimensionLength];
        memcpy(this->dimension,this->originalDimension,sizeof(int) * dimensionLength);

        //we can drop trailing dimensions where it's all singular for example:
        // shape: 4,3,1,2
        //dimension: 0,2
        // the problem for 0,2 is equivalent to: 0
        //the rest of the algorithm handles cases suchas
        //shape: 4,1,1,2
        //dimension: 0,1
        //when this happens there are other dimensions (eg: at the end) that matter
        int trailingOneDimensions = 0;
        //trailing ones
        for(int i = dimensionLength - 1; i >= 0; i--) {
            if(shape[dimension[i]] != 1) {
                break;
            }
            else if(shape[dimension[i]] == 1)
                trailingOneDimensions++;
        }

        dimensionLength -= trailingOneDimensions;

        int leadingOneDimensions = 0;
        //trailing ones
        for(int i = 0; i < dimensionLength; i++) {
            if(shape[dimension[i]] != 1) {
                break;
            }
            else if(shape[dimension[i]] == 1)
                leadingOneDimensions++;
        }

        //bump the dimension pointer forward for however many leadingones there are
        dimension += leadingOneDimensions;
        //decrease the dimension length by the amount of leading ones
        dimensionLength -= leadingOneDimensions;


        bool preConverged = true;
        for(int i = 0; i < dimensionLength; i++) {
            if(shape[dimension[i]] == 1) {
                preConverged = false;
                break;
            }
        }

        //we took away all the singular dimensions, we can just return
        if(preConverged)
            return;

        //no more singular dimensions specified
        bool done = false;
        int onesDecrement = 0;
        bool changed = false;
        while(!done) {
            //terminate early: only singular dimensions specified for reduce
            if((dimensionLength) < 1) {
                done = true;
                //signal as a no op
                dimension[0] = -1;
                break;
            }
            //captures intermediary result from the for loop
            traceNew(3);

            int intermediaryResult[MAX_RANK];
            for(int i = 0; i < dimensionLength; i++) {
                intermediaryResult[i] = (dimension)[i];
            }

            bool oneEncountered = false;
            bool nonOneEncountered = false;
            bool hitBeginning = false;
            //assume intermediate collapsing of dimensions
            bool collapseMiddleDimensions = true;
            //note here that dimension length MAY end up being zero
            for(int i = (dimensionLength) - 1; i >= 0; i--) {
                if(shape[(dimension)[i]] == 1) {
                    oneEncountered = true;
                    //trailing ones
                    if(!nonOneEncountered) {
                        //just drop trailing ones
                        dimensionLength--;
                        nonOneEncountered = false;
                        collapseMiddleDimensions = false;
                        //intermediary result just needs to have the results copied from dimension since we're just removing the tail
                        memcpy(intermediaryResult,dimension,sizeof(int) * dimensionLength);
                        changed = true;
                        //break the for loop and force it to go back around starting from the new index
                        break;
                    }
                    else {
                        //already decremented all dimensions
                        //this was a result of hitting beginning ones
                        //we will only need to loop once
                        if(i == 0) {
                            hitBeginning = true;
                        }
                        //will need to shift dimensions that aren't trailing ones
                        //back by onesDecrement
                        //mark the intermediary result as -1 for non inclusion
                        intermediaryResult[i] = -1;
                        onesDecrement++;
                    }
                }
                else {
                    intermediaryResult[i] = (dimension)[i];
                    nonOneEncountered = true;
                }
            }

            if(collapseMiddleDimensions && oneEncountered) {
                //collapse dimensions
                int newIntermediary[MAX_RANK];
                int idx = 0;
                for(int i = 0; i < dimensionLength; i++) {
                    //of note: dimension will decrease by the number of ones encountered
                    if(intermediaryResult[i] >= 0) {
                        //dimension 0 doesn't need to be decremented
                        if(intermediaryResult[i] > 0)
                            newIntermediary[idx++] = intermediaryResult[i] - onesDecrement;
                        else
                            newIntermediary[idx++] = intermediaryResult[i];
                    }
                }


                //decrement by the number of dimensions where ones appeared
                (dimensionLength) -= onesDecrement;
                //update to current result
                memcpy(dimension,newIntermediary,sizeof(int) * (dimensionLength));
                changed = true;

            }
                //converged: no need to change result
            else {
                //update to current result
                memcpy(dimension,intermediaryResult,sizeof(int) * dimensionLength);
            }

            //converge when there are no singular dimensions specified in the reduce
            done = (!oneEncountered && nonOneEncountered) || hitBeginning;
            //delete[] intermediaryResult;
        }

        //nothing changed but need to collapse dimension
        if(!changed && this->numOnes > 0) {
            for(int i = 0; i < dimensionLength ;i++) {
                dimension[i] -= numOnes;
            }
        }


    }
}