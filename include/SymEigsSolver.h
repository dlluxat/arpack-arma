#ifndef SYMEIGSSOLVER_H
#define SYMEIGSSOLVER_H

#include <armadillo>
#include <vector>
#include <algorithm>
#include <cmath>
#include <utility>
#include <stdexcept>

#include "MatOp.h"
#include "SelectionRule.h"
#include "UpperHessenbergQR.h"


template <typename Scalar = double, int SelectionRule = LARGEST_MAGN>
class SymEigsSolver
{
private:
    typedef arma::Mat<Scalar> Matrix;
    typedef arma::Col<Scalar> Vector;
    typedef arma::uvec BoolVector;
    typedef std::pair<Scalar, int> SortPair;

    MatOp<Scalar> *op;    // object to conduct matrix operation,
                          // e.g. matrix-vector product
    const int dim_n;      // dimension of matrix A

protected:
    const int nev;        // number of eigenvalues requested

private:
    const int ncv;        // number of ritz values
    int nmatop;           // number of matrix operations called
    int niter;            // number of restarting iterations

    Matrix fac_V;         // V matrix in the Arnoldi factorization
    Matrix fac_H;         // H matrix in the Arnoldi factorization
    Vector fac_f;         // residual in the Arnoldi factorization

protected:
    Vector ritz_val;      // ritz values

private:
    Matrix ritz_vec;      // ritz vectors
    BoolVector ritz_conv; // indicator of the convergence of ritz values

    const Scalar prec;    // precision parameter used to test convergence
                          // prec = epsilon^(2/3)
                          // epsilon is the machine precision,
                          // e.g. ~= 1e-16 for the "double" type

    // Matrix product in this case, and shift solve for SymEigsShiftSolver
    virtual void matrix_operation(Scalar *x_in, Scalar *y_out)
    {
        op->prod(x_in, y_out);
    }

    // Arnoldi factorization starting from step-k
    void factorize_from(int from_k, int to_m, const Vector &fk)
    {
        if(to_m <= from_k) return;

        fac_f = fk;

        Vector v(dim_n), w(dim_n);
        Scalar beta = 0.0, Hii = 0.0;
        // Keep the upperleft k x k submatrix of H and set other elements to 0
        fac_H.tail_cols(ncv - from_k).zeros();
        fac_H.submat(arma::span(from_k, ncv - 1), arma::span(0, from_k - 1)).zeros();
        for(int i = from_k; i <= to_m - 1; i++)
        {
            beta = arma::norm(fac_f);
            v = fac_f / beta;
            fac_V.col(i) = v; // The (i+1)-th column
            fac_H(i, i - 1) = beta;

            matrix_operation(v.memptr(), w.memptr());
            nmatop++;

            Hii = arma::dot(v, w);
            fac_H(i - 1, i) = beta;
            fac_H(i, i) = Hii;

            fac_f = w - beta * fac_V.col(i - 1) - Hii * v;
            // Correct f if it is not orthogonal to V
            // Typically the largest absolute value occurs in
            // the first element, i.e., <v1, f>, so we use this
            // to test the orthogonality
            Scalar v1f = arma::dot(fac_f, fac_V.col(0));
            if(v1f > prec || v1f < -prec)
            {
                Vector Vf(i + 1);
                Vf.tail(i) = fac_V.cols(1, i).t() * fac_f;
                Vf[0] = v1f;
                fac_f -= fac_V.head_cols(i + 1) * Vf;
            }
        }
    }

    // Implicitly restarted Arnoldi factorization
    void restart(int k)
    {
        if(k >= ncv)
            return;

        TridiagQR<Scalar> decomp;
        Vector em(ncv, arma::fill::zeros);
        em[ncv - 1] = 1;

        for(int i = k; i < ncv; i++)
        {
            // QR decomposition of H-mu*I, mu is the shift
            fac_H.diag() -= ritz_val[i];
            decomp.compute(fac_H);

            // V -> VQ
            decomp.apply_YQ(fac_V);
            // H -> Q'HQ
            // Since QR = H - mu * I, we have H = QR + mu * I
            // and therefore Q'HQ = RQ + mu * I
            fac_H = decomp.matrix_RQ();
            fac_H.diag() += ritz_val[i];
            // em -> Q'em
            decomp.apply_QtY(em);
        }
        Vector fk = fac_f * em[k - 1] + fac_V.col(k) * fac_H(k, k - 1);
        factorize_from(k, ncv, fk);
        retrieve_ritzpair();
    }

    // Calculate the number of converged Ritz values
    int num_converged(Scalar tol)
    {
        // thresh = tol * max(prec, abs(theta)), theta for ritz value
        Vector rv = arma::abs(ritz_val.head(nev));
        Vector thresh = tol * arma::clamp(rv, prec, rv.max());
        Vector resid = arma::abs(ritz_vec.tail_rows(1).t()) * arma::norm(fac_f);
        // Converged "wanted" ritz values
        ritz_conv = (resid < thresh);

        return arma::sum(ritz_conv);
    }

    // Return the adjusted nev for restarting
    int nev_adjusted(int nconv)
    {
        int nev_new = nev;

        // Adjust nev_new, according to dsaup2.f line 677~684 in ARPACK
        nev_new = nev + std::min(nconv, (ncv - nev) / 2);
        if(nev == 1 && ncv >= 6)
            nev_new = ncv / 2;
        else if(nev == 1 && ncv > 2)
            nev_new = 2;

        return nev_new;
    }

    // Retrieve and sort ritz values and ritz vectors
    void retrieve_ritzpair()
    {
        Vector evals(ncv);
        Matrix evecs(ncv, ncv);
        arma::eig_sym(evals, evecs, arma::symmatl(fac_H));

        std::vector<SortPair> pairs(ncv);
        EigenvalueComparator<Scalar, SelectionRule> comp;
        for(int i = 0; i < ncv; i++)
        {
            pairs[i].first = evals[i];
            pairs[i].second = i;
        }
        std::sort(pairs.begin(), pairs.end(), comp);
        // For BOTH_ENDS, the eigenvalues are sorted according
        // to the LARGEST_ALGE rule, so we need to move those smallest
        // values to the left
        // The order would be
        // Largest => Smallest => 2nd largest => 2nd smallest => ...
        // We keep this order since the first k values will always be
        // the wanted collection, no matter k is nev_updated (used in restart())
        // or is nev (used in sort_ritzpair())
        if(SelectionRule == BOTH_ENDS)
        {
            std::vector<SortPair> pairs_copy(pairs);
            for(int i = 0; i < ncv; i++)
            {
                // If i is even, pick values from the left (large values)
                // If i is odd, pick values from the right (small values)
                if(i % 2 == 0)
                    pairs[i] = pairs_copy[i / 2];
                else
                    pairs[i] = pairs_copy[ncv - 1 - i / 2];
            }
        }

        // Copy the ritz values and vectors to ritz_val and ritz_vec, respectively
        for(int i = 0; i < ncv; i++)
        {
            ritz_val[i] = pairs[i].first;
        }
        for(int i = 0; i < nev; i++)
        {
            ritz_vec.col(i) = evecs.col(pairs[i].second);
        }
    }

protected:
    // Sort the first nev Ritz pairs in decreasing magnitude order
    // This is used to return the final results
    virtual void sort_ritzpair()
    {
        std::vector<SortPair> pairs(nev);
        EigenvalueComparator<Scalar, LARGEST_MAGN> comp;
        for(int i = 0; i < nev; i++)
        {
            pairs[i].first = ritz_val[i];
            pairs[i].second = i;
        }
        std::sort(pairs.begin(), pairs.end(), comp);

        Matrix new_ritz_vec(ncv, nev);
        BoolVector new_ritz_conv(nev);

        for(int i = 0; i < nev; i++)
        {
            ritz_val[i] = pairs[i].first;
            new_ritz_vec.col(i) = ritz_vec.col(pairs[i].second);
            new_ritz_conv[i] = ritz_conv[pairs[i].second];
        }

        ritz_vec.swap(new_ritz_vec);
        ritz_conv.swap(new_ritz_conv);
    }

public:
    SymEigsSolver(MatOp<Scalar> *op_, int nev_, int ncv_) :
        op(op_),
        dim_n(op->rows()),
        nev(nev_),
        ncv(ncv_ > dim_n ? dim_n : ncv_),
        nmatop(0),
        niter(0),
        prec(std::pow(std::numeric_limits<Scalar>::epsilon(), Scalar(2.0 / 3)))
    {
        if(nev_ < 1 || nev_ >= dim_n)
            throw std::invalid_argument("nev must be greater than zero and less than the size of the matrix");

        if(ncv_ <= nev_)
            throw std::invalid_argument("ncv must be greater than nev");
    }

    // Initialization and clean-up
    virtual void init(Scalar *init_resid)
    {
        // Reset all matrices/vectors to zero
        fac_V.zeros(dim_n, ncv);
        fac_H.zeros(ncv, ncv);
        fac_f.zeros(dim_n);
        ritz_val.zeros(ncv);
        ritz_vec.zeros(ncv, nev);
        ritz_conv.zeros(nev);

        nmatop = 0;
        niter = 0;

        Vector v(init_resid, dim_n);
        Scalar vnorm = arma::norm(v);
        if(vnorm < prec)
            throw std::invalid_argument("initial residual vector cannot be zero");
        v /= vnorm;

        Vector w(dim_n);
        matrix_operation(v.memptr(), w.memptr());
        nmatop++;

        fac_H(0, 0) = arma::dot(v, w);
        fac_f = w - v * fac_H(0, 0);
        fac_V.col(0) = v;
    }
    // Initialization with random initial coefficients
    virtual void init()
    {
        Vector init_resid(dim_n, arma::fill::randu);
        init_resid -= 0.5;
        init(init_resid.memptr());
    }

    // Compute Ritz pairs and return the number of converged eigenvalues
    int compute(int maxit = 1000, Scalar tol = 1e-10)
    {
        // The m-step Arnoldi factorization
        factorize_from(1, ncv, fac_f);
        retrieve_ritzpair();
        // Restarting
        int i, nconv, nev_adj;
        for(i = 0; i < maxit; i++)
        {
            nconv = num_converged(tol);
            if(nconv >= nev)
                break;

            nev_adj = nev_adjusted(nconv);
            restart(nev_adj);
        }
        // Sorting results
        sort_ritzpair();

        niter += i + 1;

        return std::min(nev, nconv);
    }

    void info(int &iters, int &mat_ops)
    {
        iters = niter;
        mat_ops = nmatop;
    }

    // Return converged eigenvalues
    Vector eigenvalues()
    {
        int nconv = arma::sum(ritz_conv);
        Vector res(nconv);

        if(!nconv)
            return res;

        int j = 0;
        for(int i = 0; i < nev; i++)
        {
            if(ritz_conv[i])
            {
                res[j] = ritz_val[i];
                j++;
            }
        }

        return res;
    }

    // Return converged eigenvectors
    Matrix eigenvectors()
    {
        int nconv = arma::sum(ritz_conv);
        Matrix res(dim_n, nconv);

        if(!nconv)
            return res;

        Matrix ritz_vec_conv(ncv, nconv);
        int j = 0;
        for(int i = 0; i < nev; i++)
        {
            if(ritz_conv[i])
            {
                ritz_vec_conv.col(j) = ritz_vec.col(i);
                j++;
            }
        }

        res = fac_V * ritz_vec_conv;

        return res;
    }
};





template <typename Scalar = double, int SelectionRule = LARGEST_MAGN>
class SymEigsShiftSolver: public SymEigsSolver<Scalar, SelectionRule>
{
private:
    typedef arma::Col<Scalar> Vector;

    Scalar sigma;
    MatOpWithRealShiftSolve<Scalar> *op_shift;

    // Shift solve in this case
    void matrix_operation(Scalar *x_in, Scalar *y_out)
    {
        op_shift->shift_solve(x_in, y_out);
    }

    // First transform back the ritz values, and then sort
    void sort_ritzpair()
    {
        Vector ritz_val_org = Scalar(1.0) / this->ritz_val.head(this->nev) + sigma;
        this->ritz_val.head(this->nev) = ritz_val_org;
        SymEigsSolver<Scalar, SelectionRule>::sort_ritzpair();
    }
public:
    SymEigsShiftSolver(MatOpWithRealShiftSolve<Scalar> *op_,
                       int nev_, int ncv_, Scalar sigma_) :
        SymEigsSolver<Scalar, SelectionRule>(op_, nev_, ncv_),
        sigma(sigma_),
        op_shift(op_)
    {
        op_shift->set_shift(sigma);
    }
};

#endif // SYMEIGSSOLVER_H
