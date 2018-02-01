// solveUVM and solveMVM are based on R/EMMREML functions
// solveMKM is based on the mmer function in R/sommer
#include "alphasimr.h"
#include <iostream>
#include <string>

// Note: Fortran compiler appends '_' to subroutine name
// See http://www.netlib.org/lapack/explore-html/ for description of args
extern "C" void dsyevr_(char* JOBZ, char* RANGE, char* UPLO, long long int* N, double* A, long long int* LDA, double* VL,
                       double* VU, long long int* IL, long long int* IU, double* ABSTOL, long long int* M, double* W, double* Z,
                       long long int* LDZ, long long int* ISUPPZ, double* WORK, long long int* LWORK, long long int* IWORK,
                       long long int* LIWORK, long long int* INFO);

// Replacement for Armadillo's eig_sym
// Fixes an error with decompisition of large matrices on Eddie
// If calcVec = false, eigvec is not used
// It would be better to template this function
int eigen2(arma::vec& eigval, arma::mat& eigvec, arma::mat X,
           bool calcVec = true){
  char JOBZ;
  if(calcVec){
    JOBZ = 'V';
  }else{
    JOBZ = 'N';
  }
  char RANGE = 'A';
  char UPLO = 'L';
  long long int N = X.n_rows;
  // A = X
  long long int LDA = N;
  double VL = 0.0;
  double VU = 0.0;
  long long int IL = 0;
  long long int IU = 0;
  double ABSTOL = 0.0;
  long long int M = N;
  // W=eigval
  // Z=eigvec
  long long int LDZ = N;
  arma::Col<long long int> ISUPPZ(2*M);
  // WORK length to be determined
  double tmpWORK;
  long long int LWORK = -1; // To be calculated
  // IWORK length to be determined
  long long int tmpIWORK = 0;
  long long int LIWORK = -1; // To be calculated
  long long int INFO = 0;
  // Calculate LWORK and LIWORK
  dsyevr_(&JOBZ,&RANGE,&UPLO,&N,&*X.begin(),&LDA,&VL,&VU,&IL,&IU,&ABSTOL,&M,&*eigval.begin(),
          &*eigvec.begin(),&LDZ,&*ISUPPZ.begin(),&tmpWORK,&LWORK,&tmpIWORK,&LIWORK,&INFO);
  LWORK = (long long int) tmpWORK;
  LIWORK = tmpIWORK;
  // Allocate WORK and IWORK
  arma::vec WORK(LWORK);
  arma::Col<long long int> IWORK(LIWORK);
  // Perform decomposition
  dsyevr_(&JOBZ,&RANGE,&UPLO,&N,&*X.begin(),&LDA,&VL,&VU,&IL,&IU,&ABSTOL,&M,&*eigval.begin(),
          &*eigvec.begin(),&LDZ,&*ISUPPZ.begin(),&*WORK.begin(),&LWORK,&*IWORK.begin(),&LIWORK,&INFO);
  return INFO; // Return error code
}

// Objective function for REML using the EMMA algorithm
Rcpp::List objREML(double param, Rcpp::List args){
  double df = args["df"];
  arma::vec eta = args["eta"];
  arma::vec lambda = args["lambda"];
  double value = df * log(sum(eta%eta/(lambda+param)));
  value += sum(log(lambda+param));
  return Rcpp::List::create(Rcpp::Named("objective") = value,
                            Rcpp::Named("output") = 0);
}

arma::mat readMat(std::string fileName, int rows, int cols,
                  char sep=' ', int skipRows=0, int skipCols=0){
  arma::mat output(rows,cols);
  std::ifstream file(fileName.c_str());
  std::string line;
  //Skip rows
  for(arma::uword i=0; i<skipRows; ++i){
    std::getline(file,line);
  }
  //Read rows
  for(arma::uword i=0; i<rows; ++i){
    std::getline(file,line);
    std::stringstream lineStream(line);
    std::string cell;
    //Skip columns
    for(arma::uword j=0; j<skipCols; ++j){
      std::getline(lineStream,cell,sep);
    }
    //Read columns
    for(arma::uword j=0; j<cols; ++j){
      std::getline(lineStream,cell,sep);
      output(i,j) = std::atof(cell.c_str());
    }
  }
  file.close();
  return output;
}

// Produces a sum to zero design matrix with an intercept
arma::mat makeX(arma::uvec& x){
  arma::uword nTrain = x.n_elem;
  arma::uword nLevels = x.max();
  arma::mat X(nTrain,nLevels);
  if(nLevels==1){
    X.ones();
  }else{
    X.zeros();
    X.col(0).ones();
    for(arma::uword i=0; i<nTrain; ++i){
      if(x(i)==nLevels){
        X(i,arma::span(1,nLevels-1)).fill(-1.0);
      }else{
        X(i,x(i))=1;
      }
    }
  }
  return X;
}

// Produces a genotype design matrix
// z is an indicator vector matching y to individuals in G
// nGeno is the number of genotypes in the G matrix
arma::mat makeZ(arma::uvec& z, int nGeno){
  int nTrain = z.n_elem;
  arma::mat Z(nTrain,nGeno,arma::fill::zeros);
  for(arma::uword i=0; i<nTrain; ++i){
    Z(i,z(i)) = 1;
  }
  return Z;
}

// Generates weighted matrix
// Allows for heterogenous variance due to unequal replication
void sweepReps(arma::mat& X, arma::vec reps){
  reps = sqrt(reps);
  for(arma::uword i=0; i<X.n_cols; ++i){
    X.col(i) = X.col(i)%reps;
  }
}

Rcpp::List solveRRBLUP(const arma::mat& y, const arma::mat& X,
                       const arma::mat& M){
  int n = y.n_rows;
  int q = X.n_cols;
  double df = double(n)-double(q);
  double offset = log(double(n));

  // Construct system of equations for eigendecomposition
  arma::mat S = -(X*inv_sympd(X.t()*X)*X.t());
  S.diag() += 1;
  arma::mat H = M*M.t(); // Used later
  H.diag() += offset;
  S = S*H*S;

  // Compute eigendecomposition
  arma::vec eigval(n);
  arma::mat eigvec(n,n);
  eigen2(eigval, eigvec, S);

  // Drop eigenvalues
  eigval = eigval(arma::span(q,eigvec.n_cols-1)) - offset;
  eigvec = eigvec(arma::span(0,eigvec.n_rows-1),
                  arma::span(q,eigvec.n_cols-1));

  // Estimate variances and solve equations
  arma::vec eta = eigvec.t()*y;
  Rcpp::List optRes = optimize(*objREML,
                               Rcpp::List::create(
                                 Rcpp::Named("df")=df,
                                 Rcpp::Named("eta")=eta,
                                 Rcpp::Named("lambda")=eigval),
                                 1.0e-10, 1.0e10);
  double delta = optRes["parameter"];
  H.diag() += (delta-offset);
  H = inv_sympd(H);
  arma::mat XH = X.t()*H;
  arma::mat beta = solve(XH*X,XH*y);
  arma::mat u = M.t()*(H*(y-X*beta));
  double Vu = sum(eta%eta/(eigval+delta))/df;
  double Ve = delta*Vu;
  double ll = -0.5*(double(optRes["objective"])+df+df*log(2*PI/df));
  return Rcpp::List::create(Rcpp::Named("Vu")=Vu,
                            Rcpp::Named("Ve")=Ve,
                            Rcpp::Named("beta")=beta,
                            Rcpp::Named("u")=u,
                            Rcpp::Named("LL")=ll);
}

Rcpp::List solveRRBLUPMV(const arma::mat& Y, const arma::mat& X,
                         const arma::mat& M, double tol=1e-6,
                         int maxIter=1000){
  int n = Y.n_rows;
  int m = Y.n_cols;
  arma::vec eigval(n);
  arma::mat eigvec(n,n);
  eigen2(eigval, eigvec, M*M.t());
  arma::mat Yt = Y.t()*eigvec;
  arma::mat Xt = X.t()*eigvec;
  arma::mat Vu = cov(Y)/2;
  arma::mat Ve = Vu;
  arma::mat W = Xt.t()*inv_sympd(Xt*Xt.t());
  arma::mat B = Yt*W; //BLUEs
  arma::mat Gt(m,n), sigma(m,m), BNew, 
  VeNew(m,m), VuNew(m,m);
  double denom, numer;
  bool converging=true;
  int iter=0;
  while(converging){
    ++iter;
    VeNew.fill(0.0);
    VuNew.fill(0.0);
    for(arma::uword i=0; i<n; ++i){
      Gt.col(i) = eigval(i)*Vu*inv_sympd(eigval(i)*Vu+
        Ve+tol*arma::eye(m,m))*(Yt.col(i)-B*Xt.col(i));
    }
    BNew = (Yt - Gt)*W;
    for(arma::uword i=0; i<n; ++i){
      sigma = eigval(i)*Vu-(eigval(i)*Vu)*inv_sympd(eigval(i)*Vu+
        Ve+tol*arma::eye(m,m))*(eigval(i)*Vu);
      VuNew += 1.0/(double(n)*eigval(i))*(Gt.col(i)*Gt.col(i).t()+sigma);
      VeNew += 1.0/double(n)*((Yt.col(i)-BNew*Xt.col(i)-Gt.col(i))*
        (Yt.col(i)-BNew*Xt.col(i)-Gt.col(i)).t()+sigma);
    }
    denom = fabs(sum(Ve.diag()));
    if(denom>0.0){
      numer = fabs(sum(VeNew.diag()-Ve.diag()));
      if((numer/denom)<tol) converging=false;
    }
    Ve = VeNew;
    Vu = VuNew;
    B = BNew;
    if(iter>=maxIter){
      Rcpp::Rcerr<<"Warning: did not converge, reached maxIter\n";
      break;
    }
  }
  arma::mat HI = inv_sympd(kron(M*M.t(), Vu)+
    kron(arma::eye(n,n), Ve)+
    tol*arma::eye(n*m,n*m));
  arma::mat E = Y.t() - B*X.t();
  arma::mat U = kron(arma::eye(M.n_cols,M.n_cols), Vu)*kron(M.t(),
                     arma::eye(m,m))*(HI*vectorise(E)); //BLUPs
  U.reshape(m,U.n_elem/m);
  //Log Likelihood calculation
  arma::mat ll = -0.5*arma::vectorise(E).t()*HI*vectorise(E);
  ll -= double(n*m)/2.0*log(2*PI);
  double value;
  double sign;
  log_det(value, sign, kron(M*M.t(), Vu)+kron(arma::eye(n,n), Ve));
  ll -= 0.5*value*sign;
  return Rcpp::List::create(Rcpp::Named("Vu")=Vu,
                            Rcpp::Named("Ve")=Ve,
                            Rcpp::Named("beta")=B.t(),
                            Rcpp::Named("u")=U.t(),
                            Rcpp::Named("LL")=arma::as_scalar(ll),
                            Rcpp::Named("iter")=iter);
}

Rcpp::List solveRRBLUPMK(arma::mat& y, arma::mat& X,
                         arma::field<arma::mat>& Mlist,
                         int maxIter=40){
  double tol = 1e-4;
  int k = Mlist.n_elem;
  int n = y.n_rows;
  int q = X.n_cols;
  double df = double(n)-double(q);
  arma::field<arma::mat> V(k);
  for(arma::uword i=0; i<k; ++i){
    V(i) = Mlist(i)*Mlist(i).t();
  }
  arma::mat A(k+1,k+1), W0(n,n), W(n,n), WX(n,q), WQX(n,n);
  arma::vec qvec(k+1), sigma(k+1);
  double rss, ldet, llik, llik0, deltaLlik, taper, 
  value, sign;
  bool invPass;
  arma::field<arma::mat> T(k);
  sigma.fill(var(y.col(0)));
  int iter = 0;
  while(true){
    ++iter;
    W0 = V(0)*sigma(0);
    W0.diag() += sigma(k);
    for(arma::uword i=1; i<k; ++i){
      W0 += V(i)*sigma(i);
    }
    invPass = inv_sympd(W,W0);
    if(!invPass){
      W = pinv(W0);
    }
    WX = W*X;
    WQX = W - WX*solve(X.t()*WX, WX.t());
    rss = as_scalar(y.t()*WQX*y);
    sigma = sigma*(rss/df);
    WQX = WQX*(df/rss);
    log_det(value, sign, WQX);
    ldet = value*sign;
    llik = ldet/2 - df/2;
    if(iter == 1) llik0 = llik;
    deltaLlik = llik - llik0;
    llik0 = llik;
    for(arma::uword i=0; i<k; ++i){
      T(i) = WQX*V(i);
    }
    for(arma::uword i=0; i<k; ++i){
      qvec(i) = as_scalar(y.t()*T(i)*WQX*y - sum(T(i).diag()));
      for(arma::uword j=0; j<k; ++j){
        A(i,j) = accu(T(i)%T(j).t());
      }
      A(i,k) = accu(T(i)%WQX.t());
    }
    for(arma::uword j=0; j<k; ++j){
      A(k,j) = accu(WQX%T(j).t());
    }
    A(k,k) = accu(WQX%WQX.t());
    qvec(k) = as_scalar(y.t()*WQX*WQX*y - sum(WQX.diag()));
    A = pinv(A);
    qvec = A*qvec;
    if(iter == 1){
      taper = 0.5;
    }else if(iter == 2){
      taper = 0.7;
    }else{
      taper = 0.9;
    }
    sigma += taper*qvec;
    while(sigma.min() < -(1e-6)){
      sigma(sigma.index_min()) = -(1e-6);
    }
    if(iter > 1 & fabs(deltaLlik) < tol*10){
      break;
    }
    if(max(abs(qvec)) < tol){
      break;
    }
    if(iter >= maxIter){
      Rf_warning("Reached maxIter without converging");
      break;
    }
  }
  while(sigma.min() < 0.0){
    sigma(sigma.index_min()) = 0.0;
  }
  arma::mat beta(q,1), ee(n,1);
  arma::field<arma::mat> u(k);
  beta = solve(X.t()*W*X,X.t()*W*y);
  ee = y - X*beta;
  for(arma::uword i=0; i<k; ++i){
    u(i) = sigma(i)*Mlist(i).t()*W*ee;
  }
  arma::vec Vu(k), Ve(1);
  Vu = sigma(arma::span(0,k-1));
  Ve = sigma(k);
  return Rcpp::List::create(Rcpp::Named("Vu")=Vu,
                            Rcpp::Named("Ve")=Ve,
                            Rcpp::Named("beta")=beta,
                            Rcpp::Named("u")=u,
                            Rcpp::Named("LL")=llik,
                            Rcpp::Named("iter")=iter);
}

// Called by RRBLUP function
// [[Rcpp::export]]
Rcpp::List callRRBLUP(arma::mat y, arma::uvec x, arma::vec reps,
                         std::string genoTrain, int nMarker, int skip){
  int n = y.n_rows;
  arma::mat X = makeX(x);
  arma::mat M = readMat(genoTrain,n,nMarker,' ',skip,1);
  sweepReps(y,reps);
  sweepReps(X,reps);
  sweepReps(M,reps);
  return solveRRBLUP(y, X, M);
}

// Called by RRBLUP function
// [[Rcpp::export]]
Rcpp::List callRRBLUP_D(arma::mat y, arma::uvec x, arma::vec reps,
                        std::string genoTrain, int nMarker, int skip,
                        bool useHetCov){
  int n = y.n_rows;
  arma::field<arma::mat> Mlist(2);
  Mlist(0) = readMat(genoTrain,n,nMarker,' ',skip,1);
  arma::rowvec p = mean(Mlist(0),0)/2.0;
  Mlist(1) = 1-abs(Mlist(0)-1);
  arma::mat X;
  if(useHetCov){
    X = join_rows(makeX(x),mean(Mlist(1),1));
  }else{
    X = makeX(x);
  }
  sweepReps(y,reps);
  sweepReps(X,reps);
  sweepReps(Mlist(0),reps);
  sweepReps(Mlist(1),reps);
  return Rcpp::List::create(
    Rcpp::Named("ans")=solveRRBLUPMK(y, X, Mlist),
    Rcpp::Named("p")=p
  );
}

// Called by RRBLUP function
// [[Rcpp::export]]
Rcpp::List callRRBLUP_MV(arma::mat Y, arma::uvec x, arma::vec reps,
                            std::string genoTrain, int nMarker, 
                            int skip, int maxIter){
  int n = Y.n_rows;
  arma::mat X = makeX(x);
  arma::mat M = readMat(genoTrain,n,nMarker,' ',skip,1);
  sweepReps(Y,reps);
  sweepReps(X,reps);
  sweepReps(M,reps);
  return solveRRBLUPMV(Y, X, M, maxIter);
}

// Called by RRBLUP_GCA function
// [[Rcpp::export]]
Rcpp::List callRRBLUP_GCA(arma::mat y, arma::uvec x, arma::vec reps,
                          std::string genoFemale, std::string genoMale,
                          int nMarker, int skip, int maxIter){
  int n = y.n_rows;
  arma::mat X = makeX(x);
  arma::field<arma::mat> Mlist(2);
  Mlist(0) = readMat(genoFemale,n,nMarker,' ',skip,1);
  Mlist(0) = Mlist(0)*2;
  Mlist(1) = readMat(genoMale,n,nMarker,' ',skip,1);
  Mlist(1) = Mlist(1)*2;
  sweepReps(y, reps);
  sweepReps(X, reps);
  sweepReps(Mlist(0), reps);
  sweepReps(Mlist(1), reps);
  return solveRRBLUPMK(y,X,Mlist,maxIter);
}

// Called by RRBLUP_SCA function
// [[Rcpp::export]]
Rcpp::List callRRBLUP_SCA(arma::mat y, arma::uvec x, arma::vec reps,
                          std::string genoFemale, std::string genoMale,
                          int nMarker, int skip, int maxIter){
  int n = y.n_rows;
  arma::mat X = makeX(x);
  arma::field<arma::mat> Mlist(3);
  Mlist(0) = readMat(genoFemale,n,nMarker,' ',skip,1);
  Mlist(0) = Mlist(0)*2-1;
  Mlist(1) = readMat(genoMale,n,nMarker,' ',skip,1);
  Mlist(1) = Mlist(1)*2-1;
  Mlist(2) = Mlist(0)%Mlist(1);
  sweepReps(y, reps);
  sweepReps(X, reps);
  sweepReps(Mlist(0), reps);
  sweepReps(Mlist(1), reps);
  sweepReps(Mlist(2), reps);
  return solveRRBLUPMK(y,X,Mlist,maxIter);
}

