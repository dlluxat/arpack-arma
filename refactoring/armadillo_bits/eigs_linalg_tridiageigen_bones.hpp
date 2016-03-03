// Copyright (C) 2015 Yixuan Qiu
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.


//! Calculate the eigenvalues and eigenvectors of a symmetric tridiagonal matrix.
//! This class is a wrapper of the Lapack functions `_steqr`.
template<typename eT>
class TridiagEigen
  {
  private:

  blas_int n;
  Col<eT>  main_diag;  // Main diagonal elements of the matrix
  Col<eT>  sub_diag;   // Sub-diagonal elements of the matrix
  Mat<eT>  evecs;      // To store eigenvectors
  bool     computed;


  public:

  //! Default constructor. Computation can
  //! be performed later by calling the compute() method.
  inline TridiagEigen();

  //! Constructor to create an object that calculates the eigenvalues
  //! and eigenvectors of a symmetric tridiagonal matrix `mat`.
  inline TridiagEigen(const Mat<eT>& mat);

  //! Compute the eigenvalue decomposition of a symmetric tridiagonal matrix.
  inline void compute(const Mat<eT>& mat);

  //! Retrieve the eigenvalues.
  inline Col<eT> eigenvalues();

  //! Retrieve the eigenvectors.
  inline Mat<eT> eigenvectors();
  };