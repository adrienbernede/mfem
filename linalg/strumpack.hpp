// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#ifndef MFEM_STRUMPACK
#define MFEM_STRUMPACK

#include "../config/config.hpp"

#ifdef MFEM_USE_STRUMPACK
#ifdef MFEM_USE_MPI
#include "operator.hpp"
#include "hypre.hpp"

#include <mpi.h>
#include <complex>

//#include "StrumpackSparseSolverMPIDist.hpp"

#ifdef MFEM_STRUMPACK_SRC
// Only include Strumpack header when compiling the strumpack.cpp source file
#include "StrumpackSparseSolverMPIDist.hpp"
#else
// Forward declarations to avoid instantiation of strumpack classes
// whenever strumpack.hpp is included
namespace strumpack
{
template<typename scalar_t, typename index_t> class CSRMatrixMPI;
template<typename scalar_t, typename index_t>
class StrumpackSparseSolverMPIDist;
}
#include<StrumpackOptions.hpp>
  //class KrylovSolver;
  //class ReorderingStrategy;
  //class MC64Job;
#endif

namespace mfem
{

class STRUMPACKRowLocMatrix : public Operator
{
public:
   /** Creates a general parallel matrix from a local CSR matrix on each
       processor described by the I, J and data arrays. The local matrix should
       be of size (local) nrows by (global) glob_ncols. The new parallel matrix
       contains copies of all input arrays (so they can be deleted). */
   STRUMPACKRowLocMatrix(MPI_Comm comm,
                         int num_loc_rows, int first_loc_row,
                         int glob_nrows, int glob_ncols,
                         int *I, int *J, double *data);

   /** Creates a copy of the parallel matrix hypParMat in STRUMPACK's RowLoc
       format. All data is copied so the original matrix may be deleted. */
   STRUMPACKRowLocMatrix(const HypreParMatrix & hypParMat);

   ~STRUMPACKRowLocMatrix();

   void Mult(const Vector &x, Vector &y) const
   {
      mfem_error("STRUMPACKRowLocMatrix::Mult(...)\n"
                 "  matrix vector products are not supported.");
   }

   MPI_Comm GetComm() const { return comm_; }

   strumpack::CSRMatrixMPI<double,int>* getA() const { return A_; }

private:
   MPI_Comm   comm_;
   strumpack::CSRMatrixMPI<double,int>* A_;

}; // mfem::STRUMPACKRowLocMatrix

class STRUMPACKRowLocCmplxMatrix : public Operator
{
public:
   /** Creates a general parallel matrix from a local CSR matrix on each
       processor described by the I, J and data arrays. The local matrix should
       be of size (local) nrows by (global) glob_ncols. The new parallel matrix
       contains copies of all input arrays (so they can be deleted). */
   STRUMPACKRowLocCmplxMatrix(MPI_Comm comm,
                              int num_loc_rows, int first_loc_row,
                              int glob_nrows, int glob_ncols,
                              int *I, int *J, std::complex<double> *data);

   /** Creates a copy of the parallel matrix hypParMats in STRUMPACK's RowLoc
       format. All data is copied so the original matrices may be deleted.
       The two matrices do not need to have the same sparsity pattern.
   */
   STRUMPACKRowLocCmplxMatrix(const HypreParMatrix & hypParMat_R,
                              const HypreParMatrix & hypParMat_I);

   ~STRUMPACKRowLocCmplxMatrix();

   void Mult(const Vector &x, Vector &y) const
   {
      mfem_error("STRUMPACKRowLocMatrix::Mult(...)\n"
                 "  matrix vector products are not supported.");
   }

   MPI_Comm GetComm() const { return comm_; }

   strumpack::CSRMatrixMPI<std::complex<double>,int>* getA() const
   { return A_; }

private:
   MPI_Comm   comm_;
   strumpack::CSRMatrixMPI<std::complex<double>,int>* A_;

}; // mfem::STRUMPACKRowLocCmplxMatrix

/** The MFEM STRUMPACK Direct Solver class.

    The mfem::STRUMPACKSolver class uses the STRUMPACK library to perform LU
    factorization of a parallel sparse matrix. The solver is capable of handling
    double precision types. See http://portal.nersc.gov/project/sparse/strumpack
*/
template<typename scalar_t, typename integer_t>
class STRUMPACKBaseSolver : public mfem::Solver
{
protected:
   // Constructor with MPI_Comm parameter.
   STRUMPACKBaseSolver( int argc, char* argv[], MPI_Comm comm );

public:
   // Default destructor.
   virtual ~STRUMPACKBaseSolver();

   // Factor and solve the linear system y = Op^{-1} x.
   virtual void Mult( const Vector & x, Vector & y ) const = 0;

   // Set the operator.
   virtual void SetOperator( const Operator & op ) = 0;

   // Set various solver options. Refer to STRUMPACK documentation for
   // details.
   void SetFromCommandLine( );
   void SetPrintFactorStatistics( bool print_stat );
   void SetPrintSolveStatistics( bool print_stat );
   void SetRelTol( double rtol );
   void SetAbsTol( double atol );

   /**
    * STRUMPACK is an (approximate) direct solver. It can be used as a direct
    * solver or as a preconditioner. To use STRUMPACK as only a preconditioner,
    * set the Krylov solver to DIRECT. STRUMPACK also provides iterative solvers
    * which can use the preconditioner, and these iterative solvers can also be
    * used without preconditioner.
    *
    * Supported values are:
    *    AUTO:           Use iterative refinement if no HSS compression is used,
    *                    otherwise use GMRes.
    *    DIRECT:         No outer iterative solver, just a single application of
    *                    the multifrontal solver.
    *    REFINE:         Iterative refinement.
    *    PREC_GMRES:     Preconditioned GMRes.
    *                    The preconditioner is the (approx) multifrontal solver.
    *    GMRES:          UN-preconditioned GMRes. (for testing mainly)
    *    PREC_BICGSTAB:  Preconditioned BiCGStab.
    *                    The preconditioner is the (approx) multifrontal solver.
    *    BICGSTAB:       UN-preconditioned BiCGStab. (for testing mainly)
    */
   void SetKrylovSolver( strumpack::KrylovSolver method );

   /**
    * Supported reorderings are:
    *    METIS, PARMETIS, SCOTCH, PTSCOTCH, RCM
    */
   void SetReorderingStrategy( strumpack::ReorderingStrategy method );

   /**
    * MC64 performs (static) pivoting. Using a matching algorithm, it permutes
    * the sparse input matrix in order to get nonzero elements on the
    * diagonal. If the input matrix is already diagonally dominant, this
    * reordering can be disabled.
    * Possible values are:
    *    NONE:                          Don't do anything
    *    MAX_CARDINALITY:               Maximum cardinality
    *    MAX_SMALLEST_DIAGONAL:         Maximize smallest diagonal value
    *    MAX_SMALLEST_DIAGONAL_2:       Same as MAX_SMALLEST_DIAGONAL, but
    *                                   different algorithm
    *    MAX_DIAGONAL_SUM:              Maximize sum of diagonal values
    *    MAX_DIAGONAL_PRODUCT_SCALING:  Maximize the product of the diagonal
    *                                   values and perform row & column scaling
    */
  // void SetMC64Job( strumpack::MC64Job job );

private:
   void Init( int argc, char* argv[] );

protected:

   MPI_Comm      comm_;
   int           numProcs_;
   int           myid_;

   bool factor_verbose_;
   bool solve_verbose_;

   strumpack::StrumpackSparseSolverMPIDist<scalar_t, integer_t> * solver_;

}; // mfem::STRUMPACKBaseSolver class

/** The MFEM STRUMPACK Direct Solver class.

    The mfem::STRUMPACKSolver class uses the STRUMPACK library to perform LU
    factorization of a parallel sparse matrix. The solver is capable of handling
    double precision types. See http://portal.nersc.gov/project/sparse/strumpack
*/
class STRUMPACKSolver : public STRUMPACKBaseSolver<double,int>
{
public:
   // Constructor with MPI_Comm parameter.
   STRUMPACKSolver( int argc, char* argv[], MPI_Comm comm );

   // Constructor with STRUMPACK Matrix Object.
   STRUMPACKSolver( STRUMPACKRowLocMatrix & A);

   // Default destructor.
   ~STRUMPACKSolver() {}

   // Factor and solve the linear system y = Op^{-1} x.
   void Mult( const Vector & x, Vector & y ) const;

   // Set the operator.
   void SetOperator( const Operator & op );

protected:

   const STRUMPACKRowLocMatrix * APtr_;

}; // mfem::STRUMPACKSolver class

/** The MFEM STRUMPACK Direct Solver class for Complex Matrices.

    The mfem::STRUMPACKCmplxSolver class uses the STRUMPACK library to
    perform LU factorization of a parallel sparse matrix. The solver is
    capable of handling complex double precision types. See
    http://portal.nersc.gov/project/sparse/strumpack
*/
class STRUMPACKCmplxSolver :
   public STRUMPACKBaseSolver<std::complex<double>,int>
{
public:
   // Constructor with MPI_Comm parameter.
   STRUMPACKCmplxSolver( int argc, char* argv[], MPI_Comm comm );

   // Constructor with STRUMPACK Matrix Object.
   STRUMPACKCmplxSolver( STRUMPACKRowLocCmplxMatrix & A);

   // Default destructor.
   ~STRUMPACKCmplxSolver();

   // Factor and solve the linear system y = Op^{-1} x.
   void Mult( const Vector & x, Vector & y ) const;

   // Set the operator.
   void SetOperator( const Operator & op );

protected:

   const STRUMPACKRowLocCmplxMatrix * APtr_;

   mutable std::complex<double> * xPtr_;
   mutable std::complex<double> * yPtr_;

}; // mfem::STRUMPACKCmplxSolver class

} // mfem namespace

#endif // MFEM_USE_MPI
#endif // MFEM_USE_STRUMPACK
#endif // MFEM_STRUMPACK
