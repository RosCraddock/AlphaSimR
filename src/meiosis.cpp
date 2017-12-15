// [[Rcpp::depends(RcppArmadillo)]]
#include "alphasimr.h"

// Simulates crossing over during meiosis
// May be extended later to include higher ploidy levels

// Searches for an interval in x containing value
// Result reported as left most element of the interval
// Returns -1 if value is smaller than the values of x
// Returns last element if value is greater than values of x
// Set left to the smallest value of the interval to search
int intervalSearch(arma::vec x, double value, int left=0){
  // Check if crossover is before beginning
  if(x[left]>value){
    // Return error
    return -1;
  }
  int end = x.n_elem-1;
  // Check if crossover is at or past end
  if(x[end]<=value){
    return end;
  }
  // Perform search
  int right = end;
  while((right-left)>1){ // Interval can be decreased
    int middle = (left + right) / 2;
    if (x[middle] == value){
      left = middle;
      // Check if at the end of the vector
      if(left<end){
        // Check for identical values to the right
        while(x[left+1]==value){
          left += 1;
          if(left==end){
            break;
          }
        }
      }
      break;
    } else if (x[middle] > value){
      right = middle;
    }else{
      left = middle;
    }
  }
  return left;
}

//Simulates a gamete using Haldane's model for crossovers, ploidy=2
arma::Col<unsigned char> bivalent(const arma::Col<unsigned char>& chr1,
                                  const arma::Col<unsigned char>& chr2,
                                  const arma::vec& genMap){
  int nSites = chr1.n_elem;
  double genLen = genMap(nSites-1);
  arma::Col<unsigned char> gamete(nSites);
  int nCO = Rcpp::rpois(1, genLen)(0);
  if(nCO==0){
    // No CO, randomly pick a chromosome
    if(Rcpp::rbinom(1,1,0.5)(0)){
      gamete = chr1;
    }else{
      gamete = chr2;
    }
  }else{
    // COs present, Create CO locations
    arma::vec posCO(nCO,arma::fill::randu);
    posCO = posCO*genLen;
    posCO = arma::sort(posCO);
    int startPos = 0;
    int endPos;
    // Randomly pick starting chromosome and fill first segSite
    int readChr = Rcpp::rbinom(1,1,0.5)(0);
    if(readChr){
      gamete(0) = chr1(0);
    }else{
      gamete(0) = chr2(0);
    }
    for(arma::uword i=0; i<nCO; ++i){
      endPos = intervalSearch(genMap,posCO[i],startPos);
      // Fill gamete
      if(endPos>startPos){ // Check for double crossovers
        // Fill in segSites
        if(readChr){
          gamete(arma::span(startPos+1,endPos)) = chr1(arma::span(startPos+1,endPos));
        }else{
          gamete(arma::span(startPos+1,endPos)) = chr2(arma::span(startPos+1,endPos));
        }
      }
      startPos = endPos;
      // Switch chromosome
      readChr = ++readChr % 2;
    }
    // Fill in last segSites if needed
    if(endPos<(nSites-1)){
      if(readChr){
        gamete(arma::span(endPos+1,nSites-1)) = chr1(arma::span(endPos+1,nSites-1));
      }else{
        gamete(arma::span(endPos+1,nSites-1)) = chr2(arma::span(endPos+1,nSites-1));
      }
    }
  }
  return gamete;
}

// Makes crosses between diploid individuals.
// motherGeno: female genotypes
// mother: female parents
// fatherGeno: male genotypes
// father: male parents
// genMaps: chromosome genetic maps
// [[Rcpp::export]]
arma::field<arma::Cube<unsigned char> > cross2(
    const arma::field<arma::Cube<unsigned char> >& motherGeno, 
    arma::uvec mother,
    const arma::field<arma::Cube<unsigned char> >& fatherGeno, 
    arma::uvec father,
    const arma::field<arma::vec>& genMaps,
    double recombRatio){
  mother -= 1; // R to C++
  father -= 1; // R to C++
  int nChr = motherGeno.n_elem;
  int nInd = mother.n_elem;
  //Output data
  arma::field<arma::Cube<unsigned char> > geno(nChr);
  double femaleRecRate = 2/(1/recombRatio+1);
  double maleRecRate = 2/(recombRatio+1);
  //Loop through chromosomes
  for(arma::uword chr=0; chr<nChr; ++chr){
    arma::vec maleMap = maleRecRate*genMaps(chr);
    arma::vec femaleMap = femaleRecRate*genMaps(chr);
    int segSites = motherGeno(chr).n_rows;
    arma::Cube<unsigned char> tmpGeno(segSites,2,nInd);
    //Loop through individuals
    for(arma::uword ind=0; ind<nInd; ++ind){
      //Female gamete
      tmpGeno.slice(ind).col(0) = 
        bivalent(motherGeno(chr).slice(mother(ind)).col(0),
                 motherGeno(chr).slice(mother(ind)).col(1),
                 femaleMap);
      //Male gamete
      tmpGeno.slice(ind).col(1) = 
        bivalent(fatherGeno(chr).slice(father(ind)).col(0),
                 fatherGeno(chr).slice(father(ind)).col(1),
                 maleMap);
    } //End individual loop
    geno(chr) = tmpGeno;
  } //End chromosome loop
  return geno;
}

// Creates DH lines from diploid individuals
// [[Rcpp::export]]
arma::field<arma::Cube<unsigned char> > createDH2(
    const arma::field<arma::Cube<unsigned char> >& geno, 
    int nDH, const arma::field<arma::vec>& genMaps,
    double recombRatio, bool useFemale){
  int nChr = geno.n_elem;
  int nInd = geno(0).n_slices;
  double ratio;
  if(useFemale){
    ratio = 2/(1/recombRatio+1);
  }else{
    ratio = 2/(recombRatio+1);
  }
  //Output data
  arma::field<arma::Cube<unsigned char> > output(nChr);
  for(arma::uword chr=0; chr<nChr; ++chr){ //Chromosome loop
    arma::vec genMap = ratio*genMaps(chr);
    int segSites = geno(chr).n_rows;
    arma::Cube<unsigned char> tmp(segSites,2,nInd*nDH);
    for(arma::uword ind=0; ind<nInd; ++ind){ //Individual loop
      for(arma::uword i=0; i<nDH; ++i){ //nDH loop
        arma::Col<unsigned char> gamete = 
          bivalent(geno(chr).slice(ind).col(0),
                   geno(chr).slice(ind).col(1),
                   genMap);
        for(arma::uword j=0; j<2; ++j){ //ploidy loop
          tmp.slice(i+ind*nDH).col(j) = gamete;
        } //End ploidy loop
      } //End nDH loop
    } //End individual loop
    output(chr) = tmp;
  } //End chromosome loop
  return output;
}

// Makes crosses between diploid individuals.
// motherGeno: female genotypes
// mother: female parents
// fatherGeno: male genotypes
// father: male parents
// genMaps: chromosome genetic maps
// [[Rcpp::export]]
arma::field<arma::Cube<unsigned char> > crossPedigree(
    const arma::field<arma::Cube<unsigned char> >& founders, 
    arma::uvec mother,
    arma::uvec father,
    const arma::field<arma::vec>& genMaps,
    double recombRatio){
  mother -= 1; // R to C++
  father -= 1; // R to C++
  int nChr = founders.n_elem;
  int nInd = mother.n_elem;
  double femaleRecRate = 2/(1/recombRatio+1);
  double maleRecRate = 2/(recombRatio+1);
  
  typedef std::minstd_rand G;
  G g;
  typedef std::uniform_int_distribution<> D;
  D d(0,founders(0).n_slices-1);
  
  //Output data
  arma::field<arma::Cube<unsigned char> > geno(nChr);
  //Loop through chromosomes
  for(arma::uword chr=0; chr<nChr; ++chr){
    int segSites = founders(chr).n_rows;
    arma::Cube<unsigned char> tmpGeno(segSites,2,nInd);
    arma::vec maleMap = maleRecRate*genMaps(chr);
    arma::vec femaleMap = femaleRecRate*genMaps(chr);
    //Loop through individuals
    for(arma::uword ind=0; ind<nInd; ++ind){
      if (mother(ind) == -1){
        //Female gamete
        tmpGeno.slice(ind).col(0) = 
          bivalent(founders(chr).slice(d(g)).col(0),
                   founders(chr).slice(d(g)).col(1),
                   femaleMap);
      }
      else {
        //Female gamete
        tmpGeno.slice(ind).col(0) = 
          bivalent(tmpGeno.slice(mother(ind)).col(0),
                   tmpGeno.slice(mother(ind)).col(1),
                   femaleMap);           
      }
      if (father(ind) == -1) {
        //Male gamete
        tmpGeno.slice(ind).col(1) = 
          bivalent(founders(chr).slice(d(g)).col(0),
                   founders(chr).slice(d(g)).col(1),
                   maleMap);
      }
      else
      {
        //Male gamete
        tmpGeno.slice(ind).col(1) = 
          bivalent(tmpGeno.slice(father(ind)).col(0),
                   tmpGeno.slice(father(ind)).col(1),
                   maleMap);
      }
    } //End individual loop
    geno(chr) = tmpGeno;
  } //End chromosome loop
  return geno;
}
