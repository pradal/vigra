/************************************************************************/
/*                                                                      */
/*     Copyright 2006-2007 by F. Heinrich, B. Seppke, Ullrich Koethe    */
/*                                                                      */
/*    This file is part of the VIGRA computer vision library.           */
/*    The VIGRA Website is                                              */
/*        http://hci.iwr.uni-heidelberg.de/vigra/                       */
/*    Please direct questions, bug reports, and contributions to        */
/*        ullrich.koethe@iwr.uni-heidelberg.de    or                    */
/*        vigra@informatik.uni-hamburg.de                               */
/*                                                                      */
/*    Permission is hereby granted, free of charge, to any person       */
/*    obtaining a copy of this software and associated documentation    */
/*    files (the "Software"), to deal in the Software without           */
/*    restriction, including without limitation the rights to use,      */
/*    copy, modify, merge, publish, distribute, sublicense, and/or      */
/*    sell copies of the Software, and to permit persons to whom the    */
/*    Software is furnished to do so, subject to the following          */
/*    conditions:                                                       */
/*                                                                      */
/*    The above copyright notice and this permission notice shall be    */
/*    included in all copies or substantial portions of the             */
/*    Software.                                                         */
/*                                                                      */
/*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND    */
/*    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES   */
/*    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND          */
/*    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT       */
/*    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,      */
/*    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING      */
/*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR     */
/*    OTHER DEALINGS IN THE SOFTWARE.                                   */                
/*                                                                      */
/************************************************************************/

#ifndef VIGRA_watersheds3D_HXX
#define VIGRA_watersheds3D_HXX

#include "voxelneighborhood.hxx"
#include "multi_array.hxx"
#include "multi_localminmax.hxx"
#include "labelvolume.hxx"
#include "seededregiongrowing3d.hxx"
#include "watersheds.hxx"

namespace vigra
{

template <class SrcIterator, class SrcAccessor, class SrcShape,
          class DestIterator, class DestAccessor, class Neighborhood3D>
int preparewatersheds3D( SrcIterator s_Iter, SrcShape srcShape, SrcAccessor sa,
                         DestIterator d_Iter, DestAccessor da, Neighborhood3D)
{
    //basically needed for iteration and border-checks
    int w = srcShape[0], h = srcShape[1], d = srcShape[2];
    int x,y,z, local_min_count=0;
        
    //declare and define Iterators for all three dims at src
    SrcIterator zs = s_Iter;
    SrcIterator ys(zs);
    SrcIterator xs(ys);
        
    //Declare Iterators for all three dims at dest
    DestIterator zd = d_Iter;
        
    for(z = 0; z != d; ++z, ++zs.dim2(), ++zd.dim2())
    {
        ys = zs;
        DestIterator yd(zd);
        
        for(y = 0; y != h; ++y, ++ys.dim1(), ++yd.dim1())
        {
            xs = ys;
            DestIterator xd(yd);

            for(x = 0; x != w; ++x, ++xs.dim0(), ++xd.dim0())
            {
                AtVolumeBorder atBorder = isAtVolumeBorder(x,y,z,w,h,d);
                typename SrcAccessor::value_type v = sa(xs);
                // the following choice causes minima to point
                // to their lowest neighbor -- would this be better???
                // typename SrcAccessor::value_type v = NumericTraits<typename SrcAccessor::value_type>::max();
                int o = 0; // means center is minimum
                typename SrcAccessor::value_type my_v = v;
                if(atBorder == NotAtBorder)
                {
                    NeighborhoodCirculator<SrcIterator, Neighborhood3D>  c(xs), cend(c);
                    
                    do {
                        if(sa(c) < v)
                        {  
                            v = sa(c);
                            o = c.directionBit();
                        }
                        else if(sa(c) == my_v && my_v == v)
                        {
                            o =  o | c.directionBit();
                        }
                    }
                    while(++c != cend);
                }
                else
                {
                    RestrictedNeighborhoodCirculator<SrcIterator, Neighborhood3D>  c(xs, atBorder), cend(c);
                    do {
                        if(sa(c) < v)
                        {  
                            v = sa(c);
                            o = c.directionBit();
                        }
                        else if(sa(c) == my_v && my_v == v)
                        {
                            o =  o | c.directionBit();
                        }
                    }
                    while(++c != cend);
                }
                if (o==0) local_min_count++; 
                da.set(o, xd);
            }//end x-iteration
        }//end y-iteration
    }//end z-iteration
    return local_min_count;
}

template <class SrcIterator, class SrcAccessor,class SrcShape,
          class DestIterator, class DestAccessor,
          class Neighborhood3D>
unsigned int watershedLabeling3D( SrcIterator s_Iter, SrcShape srcShape, SrcAccessor sa,
                                  DestIterator d_Iter, DestAccessor da,
                                  Neighborhood3D)
{
    typedef typename DestAccessor::value_type LabelType;
    
    //basically needed for iteration and border-checks
    int w = srcShape[0], h = srcShape[1], d = srcShape[2];
    int x,y,z;
        
    //declare and define Iterators for all three dims at src
    SrcIterator zs = s_Iter;
    DestIterator zd = d_Iter;
        
    // temporary image to store region labels
    detail::UnionFindArray<LabelType> labels;
    
    // initialize the neighborhood traversers
    NeighborOffsetCirculator<Neighborhood3D> nc(Neighborhood3D::CausalFirst);
    NeighborOffsetCirculator<Neighborhood3D> nce(Neighborhood3D::CausalLast);
    ++nce;
    // pass 1: scan image from upper left front to lower right back
    // to find connected components

    // Each component will be represented by a tree of pixels. Each
    // pixel contains the scan order address of its parent in the
    // tree.  In order for pass 2 to work correctly, the parent must
    // always have a smaller scan order address than the child.
    // Therefore, we can merge trees only at their roots, because the
    // root of the combined tree must have the smallest scan order
    // address among all the tree's pixels/ nodes.  The root of each
    // tree is distinguished by pointing to itself (it contains its
    // own scan order address). This condition is enforced whenever a
    // new region is found or two regions are merged
    for(z = 0; z != d; ++z, ++zs.dim2(), ++zd.dim2())
    {
        SrcIterator ys = zs;
        DestIterator yd = zd;

        for(y = 0; y != h; ++y, ++ys.dim1(), ++yd.dim1())
        {
            SrcIterator xs = ys;
            DestIterator xd = yd;

            for(x = 0; x != w; ++x, ++xs.dim0(), ++xd.dim0())
            {
                LabelType currentLabel = labels.nextFreeLabel(); // default: new region    

                //check whether there is a special border treatment to be used or not
                AtVolumeBorder atBorder = isAtVolumeBorderCausal(x,y,z,w,h,d);
                    
                //We are not at the border!
                if(atBorder == NotAtBorder)
                {

                    nc = NeighborOffsetCirculator<Neighborhood3D>(Neighborhood3D::CausalFirst);
                
                    do
                    {            
                        //   Direction of NTraversr       Neighbor's direction bit is pointing
                        // = Direction of voxel           towards us?
                        if((sa(xs) & nc.directionBit()) || (sa(xs,*nc) & nc.oppositeDirectionBit()))
                        {
                            currentLabel = labels.makeUnion(da(xd,*nc), currentLabel);
                        }
                        ++nc;
                    }while(nc!=nce);
                }
                //we are at a border - handle this!!
                else
                {
                    nc = NeighborOffsetCirculator<Neighborhood3D>(Neighborhood3D::nearBorderDirectionsCausal(atBorder,0));
                    int j=0;
                    while(nc.direction() != Neighborhood3D::Error)
                    {
                        //   Direction of NTraversr       Neighbor's direction bit is pointing
                        // = Direction of voxel           towards us?
                        if((sa(xs) & nc.directionBit()) || (sa(xs,*nc) & nc.oppositeDirectionBit()))
                        {
                            currentLabel = labels.makeUnion(da(xd,*nc), currentLabel);
                        }
                        nc.turnTo(Neighborhood3D::nearBorderDirectionsCausal(atBorder,++j));
                    }
                }
                da.set(labels.finalizeLabel(currentLabel), xd);
            }
        }
    }

    unsigned int count = labels.makeContiguous();
    
    // pass 2: assign one label to each region (tree)
    // so that labels form a consecutive sequence 1, 2, ...
    zd = d_Iter;
    for(z=0; z != d; ++z, ++zd.dim2())
    {
        DestIterator yd(zd);

        for(y=0; y != h; ++y, ++yd.dim1())
        {
            DestIterator xd(yd);

            for(x = 0; x != w; ++x, ++xd.dim0())
            {
                da.set(labels[da(xd)], xd);
            }
        }
    }
    return count;
}


/** \addtogroup SeededRegionGrowing
*/
//@{

/********************************************************/
/*                                                      */
/*                     watersheds3D                     */
/*                                                      */
/********************************************************/

/** \brief Region Segmentation by means of the watershed algorithm.

    <b> Declarations:</b>

    \deprecatedAPI{watersheds3D}
    pass \ref MultiIteratorPage "MultiIterators" and \ref DataAccessors :
    \code
    namespace vigra {
        template <class SrcIterator, class SrcAccessor,class SrcShape,
                  class DestIterator, class DestAccessor,
                  class Neighborhood3D>
        unsigned int watersheds3D(SrcIterator s_Iter, SrcShape srcShape, SrcAccessor sa,
                                  DestIterator d_Iter, DestAccessor da,
                                  Neighborhood3D neighborhood3D);
    }
    \endcode
    use argument objects in conjunction with \ref ArgumentObjectFactories :
    \code
    namespace vigra {
        template <class SrcIterator, class SrcAccessor,class SrcShape,
                  class DestIterator, class DestAccessor,
                  class Neighborhood3D>
        unsigned int watersheds3D(triple<SrcIterator, SrcShape, SrcAccessor> src,
                                  pair<DestIterator, DestAccessor> dest,
                                  Neighborhood3D neighborhood3D);
    }
    \endcode

    use with 3D-Six-Neighborhood:
    \code
    namespace vigra {    
    
        template <class SrcIterator, class SrcAccessor,class SrcShape,
                  class DestIterator, class DestAccessor>
        unsigned int watersheds3DSix(triple<SrcIterator, SrcShape, SrcAccessor> src,
                                     pair<DestIterator, DestAccessor> dest);
                                    
    }
    \endcode

    use with 3D-TwentySix-Neighborhood:
    \code
    namespace vigra {    
    
        template <class SrcIterator, class SrcAccessor,class SrcShape,
                  class DestIterator, class DestAccessor>
        unsigned int watersheds3DTwentySix(triple<SrcIterator, SrcShape, SrcAccessor> src,
                                           pair<DestIterator, DestAccessor> dest);
                                    
    }
    \endcode
    \deprecatedEnd

    This function implements the union-find version of the watershed algorithms
    as described in

    J. Roerdink, R. Meijster: <em>"The watershed transform: definitions, algorithms,
    and parallelization strategies"</em>, Fundamenta Informaticae, 41:187-228, 2000

    The source volume is a boundary indicator such as the gradient magnitude
    of the trace of the \ref boundaryTensor(). Local minima of the boundary indicator
    are used as region seeds, and all other voxels are recursively assigned to the same 
    region as their lowest neighbor. Pass \ref vigra::NeighborCode3DSix or 
    \ref vigra::NeighborCode3DTwentySix to determine the neighborhood where voxel values 
    are compared. The voxel type of the input volume must be <tt>LessThanComparable</tt>.
    The function uses accessors. 
    
    ...probably soon in VIGRA:
    Note that VIGRA provides an alternative implementation of the watershed transform via
    \ref seededRegionGrowing3D(). It is slower, but handles plateaus better 
    and allows to keep a one pixel wide boundary between regions.
    
    <b> Usage:</b>

    <b>\#include</b> \<vigra/watersheds3D.hxx\><br>
    Namespace: vigra

    Example: watersheds3D of the gradient magnitude.

    \code
    typedef vigra::MultiArray<3,int> IntVolume;
    typedef vigra::MultiArray<3,double> DVolume;
    DVolume src(DVolume::difference_type(w,h,d));
    IntVolume dest(IntVolume::difference_type(w,h,d));

    float gauss=1;

    vigra::MultiArray<3, vigra::TinyVector<float,3> > temp(IntVolume::difference_type(w,h,d));
    vigra::gaussianGradientMultiArray(srcMultiArrayRange(vol),destMultiArray(temp),gauss);

    IntVolume::iterator temp_iter=temp.begin();
    for(DVolume::iterator iter=src.begin(); iter!=src.end(); ++iter, ++temp_iter)
        *iter = norm(*temp_iter);
    
    // find 6-connected regions
    int max_region_label = vigra::watersheds3DSix(srcMultiArrayRange(src), destMultiArray(dest));

    // find 26-connected regions
    max_region_label = vigra::watersheds3DTwentySix(srcMultiArrayRange(src), destMultiArray(dest));
    
    \endcode

    <b> Required Interface:</b>

    \code
    SrcIterator src_begin;
    SrcShape src_shape;
    DestIterator dest_begin;

    SrcAccessor src_accessor;
    DestAccessor dest_accessor;
    
    // compare src values
    src_accessor(src_begin) <= src_accessor(src_begin)

    // set result
    int label;
    dest_accessor.set(label, dest_begin);
    \endcode
*/
doxygen_overloaded_function(template <...> unsigned int watersheds3D)

template <class SrcIterator, class SrcAccessor, class SrcShape,
          class DestIterator, class DestAccessor,
          class Neighborhood3D>
unsigned int watersheds3D( SrcIterator s_Iter, SrcShape srcShape, SrcAccessor sa,
                           DestIterator d_Iter, DestAccessor da, Neighborhood3D neighborhood3D)
{
    //create temporary volume to store the DAG of directions to minima
    if ((int)Neighborhood3D::DirectionCount>7){  //If we have 3D-TwentySix Neighborhood
        
        vigra::MultiArray<3,int> orientationVolume(srcShape);

        preparewatersheds3D( s_Iter, srcShape, sa, 
                             destMultiArray(orientationVolume).first, destMultiArray(orientationVolume).second,
                             neighborhood3D);
     
        return watershedLabeling3D( srcMultiArray(orientationVolume).first, srcShape, srcMultiArray(orientationVolume).second,
                                    d_Iter, da,
                                    neighborhood3D);
    }
    else{
                
        vigra::MultiArray<3,unsigned char> orientationVolume(srcShape);

        preparewatersheds3D( s_Iter, srcShape, sa, 
                              destMultiArray(orientationVolume).first, destMultiArray(orientationVolume).second,
                              neighborhood3D);
     
        return watershedLabeling3D( srcMultiArray(orientationVolume).first, srcShape, srcMultiArray(orientationVolume).second,
                                    d_Iter, da,
                                    neighborhood3D);
    }
}

template <class SrcIterator, class SrcShape, class SrcAccessor,
          class DestIterator, class DestAccessor>
inline unsigned int watersheds3DSix( triple<SrcIterator, SrcShape, SrcAccessor> src, 
                                     pair<DestIterator, DestAccessor> dest)
{
    return watersheds3D(src.first, src.second, src.third, dest.first, dest.second, NeighborCode3DSix());
}

template <class SrcIterator, class SrcShape, class SrcAccessor,
          class DestIterator, class DestAccessor>
inline unsigned int watersheds3DTwentySix( triple<SrcIterator, SrcShape, SrcAccessor> src, 
                                           pair<DestIterator, DestAccessor> dest)
{
    return watersheds3D(src.first, src.second, src.third, dest.first, dest.second, NeighborCode3DTwentySix());
}

template <unsigned int N, class T1, class S1,
                          class T2, class S2>
inline unsigned int 
watersheds3DSix(MultiArrayView<N, T1, S1> const & source, 
                MultiArrayView<N, T2, S2> dest)
{
    vigra_precondition(source.shape() == dest.shape(),
        "watersheds3DSix(): shape mismatch between input and output.");
    return watersheds3DSix(srcMultiArrayRange(source), destMultiArray(dest));
}

template <unsigned int N, class T1, class S1,
          class T2, class S2>
inline unsigned int
watersheds3DTwentySix(MultiArrayView<N, T1, S1> const & source, 
                      MultiArrayView<N, T2, S2> dest)
{
    vigra_precondition(source.shape() == dest.shape(),
        "watersheds3DTwentySix(): shape mismatch between input and output.");
    return watersheds3DTwentySix(srcMultiArrayRange(source), destMultiArray(dest));
}

}//namespace vigra

#endif //VIGRA_watersheds3D_HXX
