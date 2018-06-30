#include "stdafx.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2\calib3d\calib3d.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2\features2d\features2d.hpp>
#include <opencv2\xfeatures2d\nonfree.hpp>
//#include <opencv2/flann/miniflann.hpp> //FLANN
#include <opencv2/dnn/dict.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>


#include <stdlib.h>
#include <time.h>

#include "EssentialMatrixEstimator.h"
#include "ransac.h"

#define FLANN_INDEX_KDTREE 0

using namespace cv;
using namespace std;

class FundamentalMatrixSevenPointEstimator {
public:
	typedef Eigen::Vector2d X_t;
	typedef Eigen::Vector2d Y_t;
	typedef Eigen::Matrix3d M_t;

	// The minimum number of samples needed to estimate a model.
	static const int kMinNumSamples = 7;

	// Estimate either 1 or 3 possible fundamental matrix solutions from a set of
	// corresponding points.
	//
	// The number of corresponding points must be exactly 7.
	//
	// @param points1  First set of corresponding points.
	// @param points2  Second set of corresponding points
	//
	// @return         Up to 4 solutions as a vector of 3x3 fundamental matrices.
	static std::vector<M_t> Estimate(const std::vector<X_t>& points1,
		const std::vector<Y_t>& points2);

	// Calculate the residuals of a set of corresponding points and a given
	// fundamental matrix.
	//
	// Residuals are defined as the squared Sampson error.
	//
	// @param points1    First set of corresponding points as Nx2 matrix.
	// @param points2    Second set of corresponding points as Nx2 matrix.
	// @param F          3x3 fundamental matrix.
	// @param residuals  Output vector of residuals.
	static void Residuals(const std::vector<X_t>& points1,
		const std::vector<Y_t>& points2, const M_t& F,
		std::vector<double>* residuals) {
		ComputeSquaredSampsonError_(points1, points2, F, residuals);
	}
};
//
extern "C" void __declspec(dllexport) __stdcall Hola()
{
	cout << "Hola" << endl;
}
//

std::vector<FundamentalMatrixSevenPointEstimator::M_t>
FundamentalMatrixSevenPointEstimator::Estimate(
	const std::vector<X_t>& points1, const std::vector<Y_t>& points2) {
	//CHECK_EQ(points1.size(), 7);
	//CHECK_EQ(points2.size(), 7);

	// Note that no normalization of the points is necessary here.

	// Setup system of equations: [points2(i,:), 1]' * F * [points1(i,:), 1]'.
	Eigen::Matrix<double, 7, 9> A;
	for (size_t i = 0; i < 7; ++i) {
		const double x0 = points1[i](0);
		const double y0 = points1[i](1);
		const double x1 = points2[i](0);
		const double y1 = points2[i](1);
		A(i, 0) = x1 * x0;
		A(i, 1) = x1 * y0;
		A(i, 2) = x1;
		A(i, 3) = y1 * x0;
		A(i, 4) = y1 * y0;
		A(i, 5) = y1;
		A(i, 6) = x0;
		A(i, 7) = y0;
		A(i, 8) = 1;
	}

	// 9 unknowns with 7 equations, so we have 2D null space.
	Eigen::JacobiSVD<Eigen::Matrix<double, 7, 9>> svd(A, Eigen::ComputeFullV);
	const Eigen::Matrix<double, 9, 9> f = svd.matrixV();
	Eigen::Matrix<double, 1, 9> f1 = f.col(7);
	Eigen::Matrix<double, 1, 9> f2 = f.col(8);

	f1 -= f2;

	// Normalize, such that lambda + mu = 1
	// and add constraint det(F) = det(lambda * f1 + (1 - lambda) * f2).

	const double t0 = f1(4) * f1(8) - f1(5) * f1(7);
	const double t1 = f1(3) * f1(8) - f1(5) * f1(6);
	const double t2 = f1(3) * f1(7) - f1(4) * f1(6);
	const double t3 = f2(4) * f2(8) - f2(5) * f2(7);
	const double t4 = f2(3) * f2(8) - f2(5) * f2(6);
	const double t5 = f2(3) * f2(7) - f2(4) * f2(6);

	Eigen::Vector4d coeffs;
	coeffs(0) = f1(0) * t0 - f1(1) * t1 + f1(2) * t2;
	coeffs(1) = f2(0) * t0 - f2(1) * t1 + f2(2) * t2 -
		f2(3) * (f1(1) * f1(8) - f1(2) * f1(7)) +
		f2(4) * (f1(0) * f1(8) - f1(2) * f1(6)) -
		f2(5) * (f1(0) * f1(7) - f1(1) * f1(6)) +
		f2(6) * (f1(1) * f1(5) - f1(2) * f1(4)) -
		f2(7) * (f1(0) * f1(5) - f1(2) * f1(3)) +
		f2(8) * (f1(0) * f1(4) - f1(1) * f1(3));
	coeffs(2) = f1(0) * t3 - f1(1) * t4 + f1(2) * t5 -
		f1(3) * (f2(1) * f2(8) - f2(2) * f2(7)) +
		f1(4) * (f2(0) * f2(8) - f2(2) * f2(6)) -
		f1(5) * (f2(0) * f2(7) - f2(1) * f2(6)) +
		f1(6) * (f2(1) * f2(5) - f2(2) * f2(4)) -
		f1(7) * (f2(0) * f2(5) - f2(2) * f2(3)) +
		f1(8) * (f2(0) * f2(4) - f2(1) * f2(3));
	coeffs(3) = f2(0) * t3 - f2(1) * t4 + f2(2) * t5;

	Eigen::VectorXd roots_real;
	Eigen::VectorXd roots_imag;
	if (!FindPolynomialRootsCompanionMatrix_(coeffs, &roots_real, &roots_imag)) {
		return {};
	}

	std::vector<M_t> models;
	models.reserve(roots_real.size());

	for (Eigen::VectorXd::Index i = 0; i < roots_real.size(); ++i) {
		const double kMaxRootImag = 1e-10;
		if (std::abs(roots_imag(i)) > kMaxRootImag) {
			continue;
		}

		const double lambda = roots_real(i);
		const double mu = 1;

		Eigen::MatrixXd F = lambda * f1 + mu * f2;

		F.resize(3, 3);

		const double kEps = 1e-10;
		if (std::abs(F(2, 2)) < kEps) {
			continue;
		}

		F /= F(2, 2);

		models.push_back(F.transpose());
	}

	return models;
}


Mat skewMat(Mat ep) { //ep es de dimension 3 x 1
	Mat epT;
	transpose(ep, epT);
	cout << ep.size() << " ep " << ep.depth() << endl;
	cout << epT.size() << " epT " << epT.depth() << endl;
	Mat skew = Mat::zeros(3, 3, CV_64FC1);
	skew.at<double>(0, 1) = -epT.at<double>(0, 2);
	skew.at<double>(0, 2) = epT.at<double>(0, 1);
	skew.at<double>(1, 0) = epT.at<double>(0, 2);
	skew.at<double>(1, 2) = -epT.at<double>(0, 0);
	skew.at<double>(2, 0) = -epT.at<double>(0, 1);
	skew.at<double>(2, 1) = epT.at<double>(0, 0);
	cout << "sdsd" << endl;
	//cout << skew << endl;
	return skew;
}

int getRank(Mat M) {
	Mat1d w, u, vt;
	SVD::compute(M, w, u, vt);
	//w es la matriz de valores no singulares
	//Asi que se busca aquellos valores no singulares que no sean 0s
	//Para ello usamos un threshold pequeño 
	Mat1b nonZeroSingularesValues = w > 0.0001;
	//y contamos el numero de valores no nulos
	int rank = countNonZero(nonZeroSingularesValues);

	return rank;

}

Mat makeInvertible(Mat ninv) {
	//int dim = ninv.rows;
	//int rank = getRank(ninv);
	Mat Sm, U, V;
	SVD::compute(ninv, Sm, U, V, SVD::FULL_UV);
	transpose(V, V);
	/*
	cout << "S" << endl << endl;
	cout << Sm << endl;
	cout << "U" << endl << endl;
	cout << U << endl;
	cout << "V" << endl << endl;
	cout << V << endl;
	*/
	Mat S = Mat::eye(Sm.rows, Sm.rows, CV_64F);
	S.at<double>(0, 0) = Sm.at<double>(0, 0);
	S.at<double>(1, 1) = Sm.at<double>(1, 0);
	S.at<double>(2, 2) = Sm.at<double>(2, 0);
	//cout << "S" << endl << endl;
	//cout << S << endl;

	Mat Ss = Mat(S, Rect(0, 0, 2, 2));
	Mat Us = Mat(U, Rect(0, 0, 2, 3));
	Mat Vs = Mat(V, Rect(0, 0, 2, 3));
	/*
	cout << "S" << endl << endl;
	cout << Ss << endl;
	cout << "U" << endl << endl;
	cout << Us << endl;
	cout << "V" << endl << endl;
	cout << Vs << endl;
	*/
	Mat I = Mat::eye(ninv.rows, ninv.rows, CV_64F);
	Mat Ust;
	transpose(Us, Ust);

	Mat inv = ninv + (I - Us*Ust); //obtenemos la matriz inversa correcta a partir de la matriz no inversa

	return inv;

	/*
	cout << "inv" << endl;
	cout << inv << endl;
	cout << "ninv" << endl << endl;
	cout << ninv << endl;
	*/

	//a partir de aqui se hace un proceso de verificacion, falta concluir
	/*
	Eigen::Matrix<double, 3, 3> ninv_e;

	ninv_e.row(0).col(0).setConstant(ninv.at<double>(0, 0));
	ninv_e.row(0).col(1).setConstant(ninv.at<double>(0, 1));
	ninv_e.row(0).col(2).setConstant(ninv.at<double>(0, 2));

	ninv_e.row(1).col(0).setConstant(ninv.at<double>(1, 0));
	ninv_e.row(1).col(1).setConstant(ninv.at<double>(1, 1));
	ninv_e.row(1).col(2).setConstant(ninv.at<double>(1, 2));

	ninv_e.row(2).col(0).setConstant(ninv.at<double>(2, 0));
	ninv_e.row(2).col(1).setConstant(ninv.at<double>(2, 1));
	ninv_e.row(2).col(2).setConstant(ninv.at<double>(2, 2));

	Eigen::EigenSolver<Eigen::Matrix3d> es(ninv_e, true);

	Eigen::VectorXcd eigenvals = es.eigenvalues();
	Eigen::MatrixXcd eigenvecs = es.eigenvectors();

	Mat eigenvalues = Mat(ninv.rows, 1, CV_64F);
	Mat eigenvector = Mat(ninv.rows, ninv.rows, CV_64F);;
	Mat eigenvaluesD = Mat::eye(ninv.rows, ninv.rows, CV_64F);

	eigenvalues.at<double>(0) = real(eigenvals[0]); //
	eigenvalues.at<double>(1) = real(eigenvals[1]); //
	eigenvalues.at<double>(2) = real(eigenvals[2]); //

	eigenvaluesD.at<double>(0, 0) = real(eigenvals[0]);
	eigenvaluesD.at<double>(1, 1) = real(eigenvals[1]);
	eigenvaluesD.at<double>(2, 2) = real(eigenvals[2]);

	eigenvector.at<double>(0, 0) = real(eigenvecs(0, 0));
	eigenvector.at<double>(0, 1) = real(eigenvecs(0, 1));
	eigenvector.at<double>(0, 2) = real(eigenvecs(0, 2));

	eigenvector.at<double>(1, 0) = real(eigenvecs(1, 0));
	eigenvector.at<double>(1, 1) = real(eigenvecs(1, 1));
	eigenvector.at<double>(1, 2) = real(eigenvecs(1, 2));

	eigenvector.at<double>(2, 0) = real(eigenvecs(2, 0));
	eigenvector.at<double>(2, 1) = real(eigenvecs(2, 1));
	eigenvector.at<double>(2, 2) = real(eigenvecs(2, 2));

	for (int i = 0; i < ninv.rows; i++) {
	if (eigenvalues.at<double>(i) <= 0.00001) {
	cout << " + " << i << endl;
	}
	}


	cout << "eigenvalues de ninv" << endl << endl;
	cout << eigenvaluesD << endl;


	cout << "eigenvectors de ninv" << endl << endl;
	cout << eigenvector << endl;
	*/

	//https://mathoverflow.net/questions/251206/transforming-a-non-invertible-matrix-into-an-invertible-matrix
	//https://eigen.tuxfamily.org/dox/GettingStarted.html
	//http://ksimek.github.io/2012/08/14/decompose/


}

//
template <typename T>
static float distancePointLine(const cv::Point_<T> point, const cv::Vec<T, 3>& line)
{
	//Line is given as a*x + b*y + c = 0
	return std::fabsf(line(0)*point.x + line(1)*point.y + line(2))
		/ std::sqrt(line(0)*line(0) + line(1)*line(1));
}

void HouseHolderQR(const cv::Mat &A, cv::Mat &Q, cv::Mat &R)
{
	assert(A.channels() == 1);
	assert(A.rows >= A.cols);
	auto sign = [](double value) { return value >= 0 ? 1 : -1; };
	const auto totalRows = A.rows;
	const auto totalCols = A.cols;
	R = A.clone();
	Q = cv::Mat::eye(totalRows, totalRows, A.type());
	for (int col = 0; col < A.cols; ++col)
	{
		cv::Mat matAROI = cv::Mat(R, cv::Range(col, totalRows), cv::Range(col, totalCols));
		cv::Mat y = matAROI.col(0);
		auto yNorm = norm(y);
		cv::Mat e1 = cv::Mat::eye(y.rows, 1, A.type());
		cv::Mat w = y + sign(y.at<double>(0, 0)) *  yNorm * e1;
		cv::Mat v = w / norm(w);
		cv::Mat vT; cv::transpose(v, vT);
		cv::Mat I = cv::Mat::eye(matAROI.rows, matAROI.rows, A.type());
		cv::Mat I_2VVT = I - 2 * v * vT;
		cv::Mat matH = cv::Mat::eye(totalRows, totalRows, A.type());
		cv::Mat matHROI = cv::Mat(matH, cv::Range(col, totalRows), cv::Range(col, totalRows));
		I_2VVT.copyTo(matHROI);
		R = matH * R;
		Q = Q * matH;
	}
}


int main() { //
	float inlierDistance = -1;
	freopen("input.txt", "r", stdin);
	freopen("output.txt", "w", stdout);
	String source1, source2;
	//source1 = "images/sofa/im15.jpg";
	//source2 = "images/sofa/im16.jpg";

	//source1 = "images/pokeball/pokeball_001.jpg";
	//source2 = "images/pokeball/pokeball_002.jpg";

	//source1 = "images/house/house001.jpg";
	//source2 = "images/house/house002.jpg";
	source1 = "images/rome/004.jpg";
	source2 = "images/rome/005.jpg";
	//source1 = "images/discos/discos001.jpg";
	//source2 = "images/discos/discos002.jpg";
	//source1 = "images/mesa/mesa001.jpg";
	//source2 = "images/mesa/mesa002.jpg";
	//source1 = "images/house2/house001.png"; //este
	//source2 = "images/house2/house002.png";


	//source1 = "images/pasillo/001.jpg";
	//source2 = "images/pasillo/002.jpg";

	Mat img1_color = imread(source1); //imagen 1
	Mat img2_color = imread(source2); //imagen 2

	Mat img1 = imread(source1, CV_LOAD_IMAGE_GRAYSCALE); //imagen 1
	Mat img2 = imread(source2, CV_LOAD_IMAGE_GRAYSCALE); //imagen 2

	Ptr<xfeatures2d::SIFT> sift = xfeatures2d::SIFT::create();
	std::vector<cv::KeyPoint> kp1, kp2;
	//detectamos los keypoints de ambas imagenes
	sift->detect(img1, kp1);
	sift->detect(img2, kp2);

	Mat kp_img1;
	cv::drawKeypoints(img1, kp1, kp_img1, Scalar::all(-1), DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
	imshow("keypoints imagen 1", kp_img1);

	Mat kp_img2;
	cv::drawKeypoints(img2, kp1, kp_img2, Scalar::all(-1), DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
	imshow("keypoints imagen 2", kp_img2);

	//Calculamos los descriptores para cada keypoint
	Mat descriptors1, descriptors2;
	sift->compute(img1, kp1, descriptors1);
	sift->compute(img2, kp2, descriptors2);

	//y realizamos el match respectivo
	//FlannBasedMatcher matcher; //version 1
	vector<vector<DMatch>> matches;

	vector< vector<DMatch> > matches12, matches21;
	Ptr<DescriptorMatcher> matcher = DescriptorMatcher::create("FlannBased");
	matcher->knnMatch(descriptors1, descriptors2, matches12, 2);
	matcher->knnMatch(descriptors2, descriptors1, matches21, 2);

	//cv::FlannBasedMatcher matcher = cv::FlannBasedMatcher(cv::makePtr<cv::flann::LshIndexParams>(12, 20, 2)); //creacion matcher mas a detalle, parece que solo funciona con ORB
	//cv::FlannBasedMatcher matcher = cv::FlannBasedMatcher(new flann::LshIndexParams(20, 10, 2)); //creacion matcher mas a detalle

	matcher->knnMatch(descriptors1, descriptors2, matches12, 2);
	matcher->knnMatch(descriptors2, descriptors1, matches21, 2);





	//usando k-nearest neighbor matcher
	//matcher.knnMatch(descriptors1, descriptors2, matches, 50); //version 1

	// ratio test proposed by David Lowe paper = 0.8
	std::vector<DMatch> good_matches;
	std::vector<Point2f> points1, points2;

	const float ratio = 0.8;
	for (int i = 0; i < matches12.size(); i++) {
		if (matches12[i][0].distance < ratio * matches12[i][1].distance) {
			good_matches.push_back(matches12[i][0]);
			points2.push_back(kp2[matches12[i][0].trainIdx].pt); //almacenamos los keypoints que hacen match en points
			points1.push_back(kp1[matches12[i][0].queryIdx].pt);
		}
	}

	Mat img_matches;


	cout << kp1.size() << " - " << kp2.size() << " - " << good_matches.size() << endl;

	//Matching de puntos, paso a paso
	/*
	for (int i = 0; i < good_matches.size(); i++) {
	vector<DMatch> sub_good_matches(good_matches.begin() + i, good_matches.begin() + i + 1);
	drawMatches(img1, kp1, img2, kp2, sub_good_matches, img_matches, Scalar::all(-1), Scalar::all(-1), std::vector<char>(), DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
	imshow("FLANN-Matcher SIFT Matches", img_matches);
	waitKey(0);
	}
	*/

	//Matching de puntos
	drawMatches(img1, kp1, img2, kp2, good_matches, img_matches, Scalar::all(-1), Scalar::all(-1), std::vector<char>(), DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
	imshow("FLANN-Matcher SIFT Matches", img_matches);

	Mat mask;
	//Calculamos la matriz fundamental
	Mat fundamental = findFundamentalMat(Mat(points1), Mat(points2), CV_FM_7POINT, 3, 0.99, mask);  // usando el método de 8 PUNTOS
	cout << "MATRIZ FUNDAMENTAL 1" << endl;
	cout << fundamental << endl;

	Mat mr;
	mask.convertTo(mr, CV_8UC1);
	//seleccionamos solo los puntos inliers
	std::vector<Point2f> m_1, m_2;
	vector<Eigen::Vector2d> p_1, p_2;
	mask.convertTo(mask, CV_8UC1);
	for (int i = 0; i < mask.rows; i++) {
		if (mask.at<uchar>(i, 0) == 1) {
			m_1.push_back(points1[i]);
			m_2.push_back(points2[i]);
			p_1.push_back(Eigen::Vector2d(points1[i].x, points1[i].y));
			p_2.push_back(Eigen::Vector2d(points2[i].x, points2[i].y));
		}
	}

	cout << "A POINTS 1 " << points1.size() << endl;
	cout << "A POINTS 2 " << points2.size() << endl;

	points1.clear();
	points2.clear();

	points1 = m_1;
	points2 = m_2;


	Mat fundamental3 = fundamental.clone();

	//FundamentalMatrixSevenPointEstimator::Estimate(const std::vector<X_t>& points1, const std::vector<Y_t>& points2) {
	vector<FundamentalMatrixSevenPointEstimator::M_t> models = FundamentalMatrixSevenPointEstimator::Estimate(p_1, p_2);

	cout << "Nueva matriz fundamental de colmap" << endl;
	for (int i = 0; i < models.size(); i++) {
		cout << models[i] << endl;
		fundamental3.at<double>(i, 0) = (models[i](0));
		fundamental3.at<double>(i, 1) = (models[i](1));
		fundamental3.at<double>(i, 2) = (models[i](2));
	}
	cout << "---------------------------------------------" << endl;
	cout << "Matriz fundamental 3" << endl;
	cout << fundamental3 << endl;
	cout << "---------------------------------------------" << endl;

	/*
	Calculo de la matriz esencial
	*/
	//vector<EssentialMatrixFivePointEstimator::M_t> E_models = EssentialMatrixFivePointEstimator::Estimate(p_1, p_2);

	RANSACOptions options;
	options.max_error = 0.02;
	options.confidence = 0.9999;
	options.min_inlier_ratio = 0.1;
	cout << "----A----" << endl;
	//RANSAK<EssentialMatrixEstimator> ransac(options);
	cout << "----B----" << endl;
	//const auto report = ransac.Estimate(p_1, p_2);
	cout << "----C----" << endl;
	//const auto _model_ = report.model;
	cout << "----D----" << endl;



	cout << "MATRIZ ESENCIAL" << endl;
	//cout << _model_ << endl;
	/*
	for (int i = 0; i < _model_.size(); i++) {
	cout << _model_[i](0) << " " << _models_[i](1) << " " << E_models[i](2) << endl;
	}*/
	cout << "=====================================" << endl;

	cout << "D POINTS 1 " << points1.size() << endl;
	cout << "D POINTS 2 " << points2.size() << endl;

	Mat img1_points1 = img1_color.clone();
	Mat img2_points2 = img2_color.clone();

	//begin: filtramos y hacemos match entre inliers points
	std::vector<cv::KeyPoint> kp1f, kp2f;

	for (int i = 0; i < kp1.size(); i++) {
		for (int j = 0; j < points1.size(); j++) {
			if (kp1.at(i).pt.x == points1.at(j).x && kp1.at(i).pt.y == points1.at(j).y) {
				kp1f.push_back(kp1.at(i));
			}
		}
	}

	for (int i = 0; i < kp2.size(); i++) {
		for (int j = 0; j < points2.size(); j++) {
			if (kp2.at(i).pt.x == points2.at(j).x && kp2.at(i).pt.y == points2.at(j).y) {
				kp2f.push_back(kp2.at(i));
			}
		}
	}

	Mat descriptors1_, descriptors2_;
	sift->compute(img1, kp1f, descriptors1_);
	sift->compute(img2, kp2f, descriptors2_);

	vector<vector<DMatch>> matches_;

	vector< vector<DMatch> > matches12_, matches21_;
	Ptr<DescriptorMatcher> matcher_ = DescriptorMatcher::create("FlannBased");
	matcher->knnMatch(descriptors1_, descriptors2_, matches12_, 2);
	matcher->knnMatch(descriptors2_, descriptors1_, matches21_, 2);


	std::vector<DMatch> good_matches2;

	points1.clear();
	points2.clear();

	cout << "points1 size " << points1.size() << endl;
	cout << "points2 size " << points2.size() << endl;

	for (int i = 0; i < matches12_.size(); i++) {
		if (matches12_[i][0].distance < ratio * matches12_[i][1].distance) {
			good_matches2.push_back(matches12_[i][0]);
			points2.push_back(kp2f[matches12_[i][0].trainIdx].pt); //almacenamos los keypoints que hacen match en points
			points1.push_back(kp1f[matches12_[i][0].queryIdx].pt);
		}
	}

	cout << "points1 size2 " << points1.size() << endl;
	cout << "points2 size2 " << points2.size() << endl;

	Mat img_matches2;

	Mat fundamental2 = findFundamentalMat(Mat(points1), Mat(points2), CV_FM_7POINT, 3, 0.99);  // usando el método de 8 PUNTOS

	cout << "MATRIZ FUNDAMENTAL 2" << endl;
	cout << fundamental2 << endl;


	//Matching de puntos, paso a paso
	/*
	cout << "good matches size " << good_matches2.size() << endl;
	for (int i = 0; i < good_matches2.size(); i++) {
	vector<DMatch> sub_good_matches(good_matches2.begin() + i, good_matches2.begin() + i + 1);
	drawMatches(img1, kp1f, img2, kp2f, sub_good_matches, img_matches2, cv::Scalar(79, 222, 60), Scalar::all(-1), std::vector<char>(), DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
	resize(img_matches2, img_matches2, Size2d(img_matches2.cols / 2, img_matches2.rows / 2), 0, 0, INTER_AREA);
	imshow("FLANN-Matcher SIFT Matches", img_matches2);
	waitKey(0);
	}
	*/
	drawMatches(img1, kp1f, img2, kp2f, good_matches2, img_matches2, cv::Scalar(79, 222, 60), Scalar::all(-1), std::vector<char>(), DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
	resize(img_matches2, img_matches2, Size2d(img_matches2.cols / 2, img_matches2.rows / 2), 0, 0, INTER_AREA);
	imshow("FLANN-Matcher SIFT Matches filtrado", img_matches2);

	//end : 

	vector<Vec3f> epilines1, epilines2;

	computeCorrespondEpilines(Mat(points1), 1, fundamental2, epilines1);
	computeCorrespondEpilines(Mat(points2), 2, fundamental2, epilines2);

	Mat img_1 = img1_color.clone();
	Mat img_2 = img2_color.clone();

	srand(time(NULL));
	RNG rng(12345);

	//forma completa
	/*
	for (size_t i = 0; i<points1.size(); i++) {
	Scalar color = Scalar(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
	color = Scalar(75, 231, 68);
	//cout << color << endl;
	Point ep1_P1 = Point(0, -epilines2[i][2] / epilines2[i][1]); //punto 1 de epiline actual
	Point ep1_P2 = Point(img1.cols, -(epilines2[i][2] + epilines2[i][0] * img1.cols) / epilines2[i][1]); //punto 2 de epiline actual
	Point delta1 = ep1_P2 - ep1_P1;
	Point unit1 = delta1 / (norm(delta1)); //hallamos el vector unitario
	Point center1 = points1[i]; //centro del punto caracteristico
	Point f1 = ep1_P1 - center1;
	float distance1 = (center1 - ep1_P1).dot(unit1);
	float r = 10;
	float a1 = delta1.dot(delta1);
	float b1 = 2 * f1.dot(delta1);
	float c1 = f1.dot(f1) - r*r;

	float discriminant1 = b1*b1 - 4 * a1*c1;

	//cout << "discriminant 1 " << discriminant1 << endl;
	//if (discriminant1 > 0) {
	cv::line(img_2,
	cv::Point(0, -epilines2[i][2] / epilines2[i][1]),
	cv::Point(img2.cols, -(epilines2[i][2] + epilines2[i][0] * img2.cols) / epilines2[i][1]),
	cv::Scalar(255, 255, 255));
	cv::circle(img_2, points2[i], 3, color, -1, CV_AA);

	cv::line(img_1,
	cv::Point(0, -epilines1[i][2] / epilines1[i][1]),
	cv::Point(img1.cols, -(epilines1[i][2] + epilines1[i][0] * img1.cols) / epilines1[i][1]),
	cv::Scalar(255, 255, 255));
	cv::circle(img_1, points1[i], 3, color, -1, CV_AA);
	//}
	}
	imshow("epipolar lines imagen 1", img_1);
	imshow("epipolar lines imagen 2", img_2);
	*/

	//forma completa 2
	cv::Scalar color(rng(256), rng(256), rng(256));
	cv::Mat outImg(img1.rows, img1.cols * 2, CV_8UC3);
	cv::Rect rect1(0, 0, img1.cols, img1.rows);
	cv::Rect rect2(img1.cols, 0, img1.cols, img1.rows);
	for (size_t i = 0; i < points1.size(); i++) {
		if (inlierDistance > 0)
		{
			if (distancePointLine(points1[i], epilines2[i]) > inlierDistance ||
				distancePointLine(points2[i], epilines1[i]) > inlierDistance)
			{
				//The point match is no inlier
				continue;
			}
		}
		/*
		* Epipolar lines of the 1st point set are drawn in the 2nd image and vice-versa
		*/

		cv::line(outImg(rect2),
			cv::Point(0, -epilines1[i][2] / epilines1[i][1]),
			cv::Point(img1.cols, -(epilines1[i][2] + epilines1[i][0] * img1.cols) / epilines1[i][1]),
			color);
		cv::circle(outImg(rect1), points1[i], 3, color, -1, CV_AA);

		cv::line(outImg(rect1),
			cv::Point(0, -epilines2[i][2] / epilines2[i][1]),
			cv::Point(img2.cols, -(epilines2[i][2] + epilines2[i][0] * img2.cols) / epilines2[i][1]),
			color);
		cv::circle(outImg(rect2), points2[i], 3, color, -1, CV_AA);
	}
	resize(outImg, outImg, Size2d(outImg.cols / 2, outImg.rows / 2), 0, 0, INTER_AREA);
	cv::imshow("geometria epipolar", outImg);

	//Aqui se debe hacer un filtro de match de puntos caracteristicos ¡PENDIENTE! 

	//Realizamos el calculo de la matriz de proyeccion
	Mat fundamentalT;

	fundamental2 = fundamental3;

	transpose(fundamental2, fundamentalT);



	Mat w, u, vt;
	SVD::compute(fundamental2, w, u, vt, SVD::FULL_UV);

	cout << "www" << endl;
	cout << w << endl;

	cout << "uuu" << endl;
	cout << u << endl;

	cout << "vtvtvt" << endl;
	cout << vt << endl;

	Mat D = Mat::eye(w.rows, w.rows, CV_64F);
	D.at<double>(0, 0) = w.at<double>(0, 0);
	D.at<double>(1, 1) = w.at<double>(1, 0);
	D.at<double>(2, 2) = w.at<double>(2, 0);

	cout << "D" << endl;
	cout << D << endl;


	cout << "matriz fundamental 2" << endl;
	cout << fundamental2 << endl;
	cout << endl;
	cout << "Obtenemos la matriz fundamental de nuevo" << endl;
	cout << u*D*vt << endl;

	Mat m = Mat::zeros(3, 1, CV_64F);
	m.at<double>(2, 0) = 1.0;

	cout << "m" << endl;
	cout << m << endl;

	double factor = 1.0;
	cout << "epipole de la segunda imagen" << endl;
	cout << factor*u*m << endl;

	Mat e__ = factor*u*m;

	e__.at<double>(0, 0) = vt.at<double>(2, 0);
	e__.at<double>(1, 0) = vt.at<double>(2, 1);
	e__.at<double>(2, 0) = vt.at<double>(2, 2);
	/**/
	cout << "Si multiplicamos la matriz fundamental x el epipole obtenemos" << endl;
	transpose(fundamental2, fundamentalT);
	cout << fundamental2*e__ << endl;




	Mat e_, U_, V_;
	SVDecomp(fundamentalT, e_, U_, V_, cv::DECOMP_SVD);

	//--------------
	cout << "Matriz U" << endl;
	cout << e_ << endl;
	cout << "Matriz D" << endl;
	cout << U_ << endl;
	cout << "Matriz V" << endl;
	cout << V_ << endl;

	transpose(V_, V_);
	Mat e_r = V_.col(2);
	//e_r.convertTo(e_r, CV_64FC1);
	Mat skewR = skewMat(e__);
	cout << "SKEW SYMMETRIC MATRIX" << endl;
	cout << skewR << endl;
	Mat Pr2; //matriz de proyeccion para imagen 2
	Mat SkF = skewR*fundamental2;
	hconcat(SkF, e__, Pr2);

	Mat P1;
	Mat I = Mat::eye(3, 3, CV_64FC1);
	Mat zero = Mat::zeros(3, 1, CV_64FC1);

	hconcat(I, zero, P1); //obtenemos la primera matriz de la camara 1 

	cout << "proyeccion 1" << endl;
	cout << P1 << endl;
	cout << "proyeccion 2" << endl;
	cout << Pr2 << endl;



	Mat P1_row1 = P1.row(0);
	Mat P1_row2 = P1.row(1);
	Mat P1_row3 = P1.row(2);

	Mat P2_row1 = Pr2.row(0);
	Mat P2_row2 = Pr2.row(1);
	Mat P2_row3 = Pr2.row(2);

	Mat row = Mat::zeros(1, 4, CV_64FC1); row.at<double>(0, 0) = 1.0;
	Mat Pr02;
	vconcat(Pr2, row, Pr02);
	Mat iPr02 = Pr02.inv();
	cout << "puntos 3D" << endl;
	Mat m_i; //punto 2D
	Mat M; //punto 3D
	for (int i = 0; i < points2.size(); i++) {
		m_i = Mat::zeros(4, 1, CV_64FC1);
		m_i.at<double>(0, 0) = points2[i].x;
		m_i.at<double>(1, 0) = points2[i].y;
		m_i.at<double>(2, 0) = 1.0;
		m_i.at<double>(3, 0) = 1.0;
		M = iPr02*m_i;
		cout << M.at<double>(1, 0) << " " << M.at<double>(2, 0) << " " << M.at<double>(3, 0) << "; " << endl;
	}
	cout << endl;

	/*
	//Puntos 3D
	Mat M33 = M.colRange(0, M.cols - 1).rowRange(0, M.rows).clone();
	cout << "puntos 3D" << endl;
	for (int i = 0; i < M33.rows; i++) {
	cout << M33.at<double>(i, 0) << " " << M33.at<double>(i, 1) << " " << M33.at<double>(i, 2) << ";" << endl;;
	}
	cout << endl;
	*/

	ofstream outputPLY; //archivo de salida PLY
	outputPLY.open("output.ply");

	outputPLY << "ply" << endl;
	outputPLY << "format ascii 1.0" << endl;
	outputPLY << "comment written by Leonel Ocsa Sanchez" << endl;
	outputPLY << "element vertex " << points1.size() << endl;
	outputPLY << "property float32 x" << endl;
	outputPLY << "property float32 y" << endl;
	outputPLY << "property float32 z" << endl;
	outputPLY << "property uchar red" << endl;
	outputPLY << "property uchar green" << endl;
	outputPLY << "property uchar blue" << endl;
	outputPLY << "end_header" << endl;
	outputPLY << endl;

	//recorremos los mejores matches y para cada par construimos una matriz A que cumplira con AX = 0 de este modo obtenemos los puntos 3D
	for (int i = 0; i < points1.size(); i++) {
		//cout << "id " << good_matches[i].queryIdx << endl;
		Mat A_row1 = points1[i].x*P1_row3 - P1_row1;
		Mat A_row2 = points1[i].y*P1_row3 - P1_row2;
		Mat A_row3 = points2[i].x*P2_row3 - P2_row1;
		Mat A_row4 = points2[i].y*P2_row3 - P2_row2;
		/*
		cout << A_row1 << endl;
		cout << A_row2 << endl;
		cout << A_row3 << endl;
		cout << A_row4 << endl;
		*/
		Mat A;
		A.push_back(A_row1);
		A.push_back(A_row2);
		A.push_back(A_row3);
		A.push_back(A_row4);

		//cout << A << endl;

		//cout << "Mat A" << endl;
		//cout << A << endl;
		//cout << endl;
		Mat U1, D1, V1;
		SVDecomp(A, D1, U1, V1, cv::DECOMP_SVD);
		/*
		cout << "U" << endl;
		cout << U1 << endl;
		cout << "D" << endl;
		cout << D1 << endl;
		cout << "V" << endl;
		cout << V1 << endl;
		*/
		double X, Y, Z;

		X = V1.at<double>(3, 1);
		Y = V1.at<double>(3, 2);
		Z = V1.at<double>(3, 3);

		/*
		Z = A_row3.at<double>(3) / (A_row3.at<double>(0)*A_row4.at<double>(2) + A_row3.at<double>(1)*A_row2.at<double>(2) + A_row3.at<double>(2));
		//cout << A_row3.at<double>(0) << " * " << A_row1.at<double>(2) << " + " << A_row3.at<double>(1) << " * " << A_row2.at<double>(2) << " + " << A_row3.at<double>(2) << endl;
		X = A_row4.at<double>(2)*Z;
		Y = A_row2.at<double>(2)*Z;
		*/
		//cout <<"("<<X<<","<<Y<<","<< Z <<")"<< endl;
		//cout << "R = " << A_row3.at<double>(0)*X + A_row3.at<double>(1)*Y + A_row3.at<double>(2)*Z + A_row3.at<double>(3) << endl;

		//cout << "------------------ " << endl;
		//cout << "------------------ " << endl;
		//cout << "------------------ " << endl;


		//cout << X << " ";
		//cout << Y << " ";
		//cout << Z << "; " << endl;
		/*
		cout << Z << " ";
		cout << (int)(img1_color.at<Vec3b>(Point(points1[i].x, points1[i].y))[0]) << " ";
		cout << (int)(img1_color.at<Vec3b>(Point(points1[i].x, points1[i].y))[1]) << " ";
		cout << (int)(img1_color.at<Vec3b>(Point(points1[i].x, points1[i].y))[2]) << endl;
		*/
		outputPLY << X << " ";
		outputPLY << Y * 1000 << " ";
		outputPLY << Z << " ";
		outputPLY << (int)(img1_color.at<Vec3b>(Point(points1[i].x, points1[i].y))[0]) << " ";
		outputPLY << (int)(img1_color.at<Vec3b>(Point(points1[i].x, points1[i].y))[1]) << " ";
		outputPLY << (int)(img1_color.at<Vec3b>(Point(points1[i].x, points1[i].y))[2]) << endl;

		//Mat XX = Mat::zeros(4,1, A.type());
		//Mat respuesta;

		//solve(A, XX, respuesta, DECOMP_SVD);

		//cout << respuesta << endl;



		//cout << A << endl;
		//cout << "------------------ " << endl;
		//cout << "------------------ " << endl;
		//cout << "------------------ " << endl;
	}

	outputPLY.close();

	cout << "======================================================================================================" << endl;
	cout << "================================================= QR =================================================" << endl;
	Mat Q;
	Mat R;
	Mat M_ = Mat(Pr2, Rect(0, 0, 3, 3));
	//Mat MC = Mat(Pr2, Rect(3, 0, 3, 3));
	cout << "======================================= Matriz de Proyección 2 =======================================" << endl << endl;
	cout << Pr2 << endl;
	cout << "============================================== Matriz M ==============================================" << endl << endl;
	cout << M_ << endl;
	cout << "========================================== Matriz M inversa ==========================================" << endl << endl;
	Mat Mi = M_.inv();
	cout << Mi << endl;
	HouseHolderQR(M_, Q, R);
	cout << "============================================== Matriz Q ==============================================" << endl << endl;
	cout << Q << endl;
	cout << "============================================== Matriz R ==============================================" << endl << endl;
	cout << R << endl;
	cout << "================================== Calculo del centro de la camara C =================================" << endl << endl;
	cout << "============================================== M.inv()*M =============================================" << endl << endl;
	Mat MiM = Mi*M_;
	cout << MiM << endl << endl;
	cout << "Se aprecia que M no tiene inversa" << endl;
	//cout << "============================================== Matriz MC =============================================" << endl << endl;
	//cout << MC << endl << endl;

	cout << "make invertible" << endl;
	Mat Minv = makeInvertible(M_);

	cout << "Matriz pseudoinversa" << endl << endl;
	cout << Minv << endl << endl;

	//Con la nueva matriz inversa podemos ahora definir una nueva matriz de proyeccion y ver que resultados obtenemos
	Mat newPr2;
	hconcat(Minv, e__, Pr2);

	Mat newP1_row1 = P1.row(0);
	Mat newP1_row2 = P1.row(1);
	Mat newP1_row3 = P1.row(2);

	Mat newP2_row1 = Pr2.row(0);
	Mat newP2_row2 = Pr2.row(1);
	Mat newP2_row3 = Pr2.row(2);

	cout << "NUEVOS PUNTOS 3D despues de encontrar la pseudoinversa" << endl << endl;

	for (int i = 0; i < points1.size(); i++) {
		//cout << "id " << good_matches[i].queryIdx << endl;
		Mat A_row1 = points1[i].x*newP1_row3 - newP1_row1;
		Mat A_row2 = points1[i].y*newP1_row3 - newP1_row2;
		Mat A_row3 = points2[i].x*newP2_row3 - newP2_row1;
		Mat A_row4 = points2[i].y*newP2_row3 - newP2_row2;

		Mat A;
		A.push_back(A_row1);
		A.push_back(A_row2);
		A.push_back(A_row3);
		A.push_back(A_row4);

		Mat U1, D1, V1;
		SVDecomp(A, D1, U1, V1, cv::DECOMP_SVD);

		double X, Y, Z;

		X = V1.at<double>(3, 1);
		Y = V1.at<double>(3, 2);
		Z = V1.at<double>(3, 3);

		cout << X << " ";
		cout << Y << " ";
		cout << Z << "; " << endl;
	}


	//BA
	//Ptr<BundleAdjusterBase> adjuster;


	cout << "-----------------------PUNTOS-3D-INICIO--------------------------" << endl;

	for (int i = 0; i < points1.size(); i++) {
		Scalar color = Scalar(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
		circle(img1_points1, Point2f(points1.at(i).x, points1.at(i).y), 4, color);
	}

	imshow("Image 1 points", img1_points1);

	for (int i = 0; i < points2.size(); i++) {
		Scalar color = Scalar(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
		circle(img2_points2, Point2f(points2.at(i).x, points2.at(i).y), 4, color);
	}

	imshow("Image 2 points", img2_points2);

	/*
	//convertimos de 2D a 3D
	for (int i = 0; i < points1.size(); i++) {
	//obtenemos las submatriz de P2 donde obviamos la ultima columna
	Mat Pr2sub1 = Mat(Pr2, Rect(0, 0, 3, 3));
	Mat Pr2sub2 = Mat(Pr2, Rect(3, 0, 1, 3));

	//cout << "A" << endl;
	//cout << Pr2sub1 << endl;
	//cout << "A" << endl;
	//cout << Pr2sub2 << endl;
	//cout << "A" << endl;

	//calculamos para cada punto 2D su respectivo punto 3D
	Pr2sub2.at<double>(0, 0) = points1[i].x - Pr2sub2.at<double>(0, 0);
	Pr2sub2.at<double>(1, 0) = points1[i].y - Pr2sub2.at<double>(1, 0);
	Pr2sub2.at<double>(2, 0) = 1.f - Pr2sub2.at<double>(2, 0); //tercer valor es 1

	Mat X_1, X_2;

	//Pr2sub1.convertTo(Pr2sub1, CV_64FC1);
	//cout << "inv " << Pr2sub1.inv(DECOMP_LU) << endl;
	//Pr2sub1.convertTo(Pr2sub1, CV_64FC1);
	Mat inv2;
	invert(Pr2sub1, inv2);

	Mat S;
	hconcat(Pr2sub1, Pr2sub2, S);
	S.convertTo(S, CV_64F);

	double m[3][4];	double m2[3][4];

	for (int a = 0; a < S.rows; a++) {
	for (int b = 0; b < S.cols; b++) {
	m[a][b] = S.at<double>(a, b);
	m2[a][b] = m[a][b];
	}
	}

	vector <double> v = gaussianElimination(m);

	//for (int c = 0; c < v.size(); c++)
	//cout << " -> " << v[c] << endl;

	for (int a = 0; a < N; a++) {
	double sum = 0;
	for (int b = 0; b < N; b++) {
	sum += v[b] * m2[a][b];
	}
	//printf("%.30lf ", sum);
	}


	cout << v[0] / 1000000000000000000.0 << "\t\t";
	cout << v[1] / 1000000000000000000.0 << "\t\t";
	cout << v[2] / 1000000000000000000.0 << "; "<<endl;
	//cout << v[0] << "; ";

	//getchar();
	}*/

	cout << "--------------------PUNTOS-3D-FIN--------------------------" << endl;


	/*
	RNG rng(12345);

	for (int i = 0; i < points1.size(); i++) {
	Scalar color = Scalar(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
	circle(img1_points1, Point2f(points1.at(i).x, points1.at(i).y), 4, color);
	}

	imshow("Image 1 points", img1_points1);

	for (int i = 0; i < points2.size(); i++) {
	Scalar color = Scalar(rng.uniform(0, 255), rng.uniform(0, 255), rng.uniform(0, 255));
	circle(img2_points2, Point2f(points2.at(i).x, points2.at(i).y), 4, color);
	}

	imshow("Image 2 points", img2_points2);
	*/
	waitKey(0);

	return 0;
}

//https://forum.unity.com/threads/tutorial-using-c-opencv-within-unity.459434/

/*
Ejemplo de parametros de camara

Camera Intrinsics
IntrinsicMatrix: [3x3 double]
FocalLength: [1.0376e+03 1.0433e+03]
PrincipalPoint: [642.2316 387.8358]
Skew: 0

Lens Distortion
RadialDistortion: [0.1469 -0.2144]
TangentialDistortion: [0 0]

Camera Extrinsics
RotationMatrices: [3x3x140 double]
TranslationVectors: [140x3 double]

Accuracy of Estimation
MeanReprojectionError: 0.2972
ReprojectionErrors: [54x2x140 double]
ReprojectedPoints: [54x2x140 double]

Calibration Settings
NumPatterns: 140
WorldPoints: [54x2 double]
WorldUnits: 'mm'
EstimateSkew: 0
NumRadialDistortionCoefficients: 2
EstimateTangentialDistortion: 0



*/