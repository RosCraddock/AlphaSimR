// [[Rcpp::depends(RcppArmadillo)]]
#include "alphasimr.h"

/*
 * Genotype data is stored in a field of cubes.
 * The field has length equal to nChr
 * Each cube has dimensions nLoci by ploidy by nInd
 */
// [[Rcpp::export]]
arma::Mat<unsigned char> getGeno(const arma::field<arma::Cube<unsigned char> >& geno, 
                                 const arma::ivec& lociPerChr,
                                 arma::uvec lociLoc){
  // R to C++ index correction
  lociLoc -= 1;
  
  int nInd = geno(0).n_slices;
  int nChr = geno.n_elem;
  arma::Mat<unsigned char> output(nInd,arma::sum(lociPerChr));
  int loc1;
  int loc2 = -1;
  for(int i=0; i<nChr; ++i){
    if(lociPerChr(i)>0){
      // Get loci locations
      loc1 = loc2+1;
      loc2 += lociPerChr(i);
      arma::uvec chrLociLoc = lociLoc(arma::span(loc1,loc2));
      // Get chromsome genotype
      arma::Mat<unsigned char> tmp;
      tmp = arma::sum(geno(i),1);
      // Assign genotypes to output matrix
      output.cols(loc1,loc2) = (tmp.rows(chrLociLoc)).t();
    }
  }
  return output;
}

// Converts geno matrix to dominance indicator matrix when ploidy=2
// [[Rcpp::export]]
arma::Mat<unsigned char> getDomGeno(arma::Mat<unsigned char> geno){
  return geno.replace(2,0);
}

// Returns haplotype data in a matrix of nInd*ploidy by nLoci
// [[Rcpp::export]]
arma::Mat<unsigned char> getHaplo(const arma::field<arma::Cube<unsigned char> >& geno, 
                                  const arma::ivec& lociPerChr,
                                  arma::uvec lociLoc){
  // R to C++ index correction
  lociLoc -= 1;
  
  int nInd = geno(0).n_slices;
  int nChr = geno.n_elem;
  int ploidy = geno(0).n_cols;
  arma::Mat<unsigned char> output(nInd*ploidy,arma::sum(lociPerChr));
  int loc1;
  int loc2 = -1;
  // Get chromosome data
  for(int i=0; i<nChr; ++i){
    if(lociPerChr(i)>0){
      // Get loci locations
      loc1 = loc2+1;
      loc2 += lociPerChr(i);
      arma::uvec chrLociLoc = lociLoc(arma::span(loc1,loc2));
      // Get individual data
      for(int ind=0; ind<nInd; ++ind){
        output(arma::span(ind*ploidy,(ind+1)*ploidy-1),
               arma::span(loc1,loc2)) = 
                 (geno(i).slice(ind).rows(chrLociLoc)).t();
      }
    }
  }
  return output;
}

// Returns haplotype data in a matrix of nInd by nLoci for a single
// chromosome group. i.e. just female or male chromosomes for diploids
// [[Rcpp::export]]
arma::Mat<unsigned char> getOneHaplo(const arma::field<arma::Cube<unsigned char> >& geno, 
                                     const arma::ivec& lociPerChr,
                                     arma::uvec lociLoc, int haplo){
  // R to C++ index correction
  lociLoc -= 1;
  haplo -= 1;
  
  int nInd = geno(0).n_slices;
  int nChr = geno.n_elem;
  arma::Mat<unsigned char> output(nInd,arma::sum(lociPerChr));
  int loc1;
  int loc2 = -1;
  arma::uvec colSel(1);
  colSel(0) = haplo;
  // Get chromosome data
  for(int i=0; i<nChr; ++i){
    if(lociPerChr(i)>0){
      // Get loci locations
      loc1 = loc2+1;
      loc2 += lociPerChr(i);
      arma::uvec chrLociLoc = lociLoc(arma::span(loc1,loc2));
      // Get individual data
      for(int ind=0; ind<nInd; ++ind){
        output(ind,arma::span(loc1,loc2)) = 
          (geno(i).slice(ind).submat(chrLociLoc,colSel)).t();
      }
    }
  }
  return output;
}

// [[Rcpp::export]]
void writeGeno(const arma::field<arma::Cube<unsigned char> >& geno, 
               const arma::ivec& lociPerChr,
               arma::uvec lociLoc,
               Rcpp::String filePath){
  arma::Mat<unsigned char> output;
  output = getGeno(geno,lociPerChr,lociLoc);
  std::ofstream outFile;
  outFile.open(filePath, std::ios_base::app);
  output.save(outFile,arma::raw_ascii);
  outFile.close();
}

// [[Rcpp::export]]
void writeOneHaplo(const arma::field<arma::Cube<unsigned char> >& geno, 
                   const arma::ivec& lociPerChr, 
                   arma::uvec lociLoc, int haplo,
                   Rcpp::String filePath){
  arma::Mat<unsigned char> output;
  output = getOneHaplo(geno,lociPerChr,lociLoc,haplo);
  std::ofstream outFile;
  outFile.open(filePath, std::ios_base::app);
  output.save(outFile,arma::raw_ascii);
  outFile.close();
}