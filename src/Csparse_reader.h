#ifndef BEACHMAT_CSPARSE_READER_H
#define BEACHMAT_CSPARSE_READER_H

#include "beachmat.h"
#include "utils.h"
#include "dim_checker.h"

namespace beachmat {

/*** Class definition ***/

template<typename T, class V>
class Csparse_reader : public dim_checker {
public:    
    Csparse_reader(const Rcpp::RObject&);
    ~Csparse_reader();

    T get(size_t, size_t);

    template <class Iter>
    void get_row(size_t, Iter, size_t, size_t);

    template <class Iter>
    void get_col(size_t, Iter, size_t, size_t);

    size_t get_const_col_nonzero(size_t, Rcpp::IntegerVector::iterator&, typename V::iterator&, size_t, size_t);

    Rcpp::RObject yield () const;
    matrix_type get_matrix_type () const;
protected:
    Rcpp::RObject original;
    Rcpp::IntegerVector i, p;
    V x;

    size_t currow, curstart, curend;
    std::vector<int> indices; // Left as 'int' to simplify comparisons with 'i' and 'p'.
    void update_indices(size_t, size_t, size_t);

    static T get_empty(); // Specialized function for each realization (easy to extend for non-int/double).
};

/*** Constructor definition ***/

template <typename T, class V>
Csparse_reader<T, V>::Csparse_reader(const Rcpp::RObject& incoming) : original(incoming), currow(0), curstart(0), curend(this->ncol) {
    std::string ctype=check_Matrix_class(incoming, "gCMatrix");  
    this->fill_dims(get_safe_slot(incoming, "Dim"));
    const size_t& NC=this->ncol;
    const size_t& NR=this->nrow;

    Rcpp::RObject temp_i=get_safe_slot(incoming, "i");
    if (temp_i.sexp_type()!=INTSXP) { throw_custom_error("'i' slot in a ", ctype, " object should be integer"); }
    i=temp_i;

    Rcpp::RObject temp_p=get_safe_slot(incoming, "p");
    if (temp_p.sexp_type()!=INTSXP) { throw_custom_error("'p' slot in a ", ctype, " object should be integer"); }
    p=temp_p;

    Rcpp::RObject temp_x=get_safe_slot(incoming, "x");
    if (temp_x.sexp_type()!=x.sexp_type()) { 
        std::stringstream err;
        err << "'x' slot in a " << get_class(incoming) << " object should be " << translate_type(x.sexp_type());
        throw std::runtime_error(err.str().c_str());
    }
    x=temp_x;

    if (x.size()!=i.size()) { throw_custom_error("'x' and 'i' slots in a ", ctype, " object should have the same length"); }
    if (NC+1!=p.size()) { throw_custom_error("length of 'p' slot in a ", ctype, " object should be equal to 'ncol+1'"); }
    if (p[0]!=0) { throw_custom_error("first element of 'p' in a ", ctype, " object should be 0"); }
    if (p[NC]!=x.size()) { throw_custom_error("last element of 'p' in a ", ctype, " object should be 'length(x)'"); }

    // Checking all the indices.
    auto pIt=p.begin();
    for (size_t px=0; px<NC; ++px) {
        const int& current=*pIt;
        if (current < 0) { throw_custom_error("'p' slot in a ", ctype, " object should contain non-negative values"); }
        if (current > *(++pIt)) { throw_custom_error("'p' slot in a ", ctype, " object should be sorted"); }
    }

    pIt=p.begin();
    for (size_t px=0; px<NC; ++px) {
        int left=*pIt; // Integers as that's R's storage type. 
        int right=*(++pIt)-1; // Not checking the last element, as this is the start of the next column.
        auto iIt=i.begin()+left;

        for (int ix=left; ix<right; ++ix) {
            const int& current=*iIt;
            if (current > *(++iIt)) {
                throw_custom_error("'i' in each column of a ", ctype, " object should be sorted");
            }
        }
    }

    for (auto iIt=i.begin(); iIt!=i.end(); ++iIt) {
        const int& curi=*iIt;
        if (curi<0 || curi>=NR) {
            throw_custom_error("'i' slot in a ", ctype, " object should contain elements in [0, nrow)");
        }
    }

    return;
}

template <typename T, class V>
Csparse_reader<T, V>::~Csparse_reader () {}

/*** Getter functions ***/

template <typename T, class V>
T Csparse_reader<T, V>::get(size_t r, size_t c) {
    check_oneargs(r, c);
    auto iend=i.begin() + p[c+1];
    auto loc=std::lower_bound(i.begin() + p[c], iend, r);
    if (loc!=iend && *loc==r) { 
        return x[loc - i.begin()];
    } else {
        return get_empty();
    }
}

template <typename T, class V>
void Csparse_reader<T, V>::update_indices(size_t r, size_t first, size_t last) {
    /* Initializing the indices upon the first request, assuming currow=0 based on initialization above.
     * This avoids using up space for the indices if we never do row access.
     */
    if (indices.size()!=this->ncol) {
        indices=std::vector<int>(p.begin(), p.begin()+this->ncol);
    }

    /* If left/right slice are not equal to what is stored, we reset the indices,
     * so that the code below will know to recompute them. It's too much effort
     * to try to figure out exactly which columns need recomputing; just do them all.
     */
    if (first!=curstart || last!=curend) {
        curstart=first;
        curend=last;
        Rcpp::IntegerVector::iterator pIt=p.begin()+first;
        for (size_t px=first; px<last; ++px, ++pIt) {
            indices[px]=*pIt; 
        }
        currow=0;
    }

    /* entry of 'indices' for each column should contain the index of the first
     * element with row number not less than 'r'. If no such element exists, it
     * will contain the index of the first element of the next column.
     */
    if (r==currow) { 
        return; 
    } 

    Rcpp::IntegerVector::iterator pIt=p.begin()+first;
    if (r==currow+1) {
        ++pIt; // points to the first-past-the-end element, at any given 'c'.
        for (size_t c=first; c<last; ++c, ++pIt) {
            int& curdex=indices[c];
            if (curdex!=*pIt && i[curdex] < r) { 
                ++curdex;
            }
        }
    } else if (r+1==currow) {
        for (size_t c=first; c<last; ++c, ++pIt) {
            int& curdex=indices[c];
            if (curdex!=*pIt && i[curdex-1] >= r) { 
                --curdex;
            }
        }

    } else { 
        Rcpp::IntegerVector::iterator istart=i.begin(), loc;
        if (r > currow) {
            ++pIt; // points to the first-past-the-end element, at any given 'c'.
            for (size_t c=first; c<last; ++c, ++pIt) { 
                int& curdex=indices[c];
                loc=std::lower_bound(istart + curdex, istart + *pIt, r);
                curdex=loc - istart;
            }
        } else { 
            for (size_t c=first; c<last; ++c, ++pIt) {
                int& curdex=indices[c];
                loc=std::lower_bound(istart + *pIt, istart + curdex, r);
                curdex=loc - istart;
            }
        }
    }

    currow=r;
    return;
}

template <typename T, class V>
template <class Iter>
void Csparse_reader<T, V>::get_row(size_t r, Iter out, size_t first, size_t last) {
    check_rowargs(r, first, last);
    update_indices(r, first, last);
    std::fill(out, out+last-first, get_empty());

    auto pIt=p.begin()+first+1; // Points to first-past-the-end for each 'c'.
    for (size_t c=first; c<last; ++c, ++pIt, ++out) { 
        const int& idex=indices[c];
        if (idex!=*pIt && i[idex]==r) { (*out)=x[idex]; }
    } 
    return;  
}

template <typename T, class V>
template <class Iter>
void Csparse_reader<T, V>::get_col(size_t c, Iter out, size_t first, size_t last) {
    check_colargs(c, first, last);
    const int& pstart=p[c]; 
    auto iIt=i.begin()+pstart, 
         eIt=i.begin()+p[c+1]; 
    auto xIt=x.begin()+pstart;

    if (first) { // Jumping ahead if non-zero.
        auto new_iIt=std::lower_bound(iIt, eIt, first);
        xIt+=(new_iIt-iIt);
        iIt=new_iIt;
    } 
    if (last!=(this->nrow)) { // Jumping to last element.
        eIt=std::lower_bound(iIt, eIt, last);
    }

    std::fill(out, out+last-first, get_empty());
    for (; iIt!=eIt; ++iIt, ++xIt) {
        *(out + (*iIt - int(first)))=*xIt;
    }
    return;
}

template <typename T, class V>
size_t Csparse_reader<T, V>::get_const_col_nonzero(size_t c, Rcpp::IntegerVector::iterator& index, typename V::iterator& val, size_t first, size_t last) {
    check_colargs(c, first, last);
    const int& pstart=p[c]; 
    index=i.begin()+pstart;
    auto endex=i.begin()+p[c+1]; 
    val=x.begin()+pstart;

    if (first) { // Jumping ahead if non-zero.
        auto new_index=std::lower_bound(index, endex, first);
        val+=(new_index-index);
        index=new_index;
    } 
    if (last!=(this->nrow)) { // Jumping to last element.
        endex=std::lower_bound(index, endex, last);
    }

    return endex - index;
}

template<typename T, class V>
Rcpp::RObject Csparse_reader<T, V>::yield() const {
    return original;
}

template<typename T, class V>
matrix_type Csparse_reader<T, V>::get_matrix_type() const {
    return SPARSE;
}

}

#endif
