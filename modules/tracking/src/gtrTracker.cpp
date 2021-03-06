/*///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/
#include "precomp.hpp"

#ifdef HAVE_OPENCV_DNN
#include "opencv2/dnn.hpp"
#endif

namespace cv {
inline namespace tracking {

TrackerGOTURN::TrackerGOTURN()
{
    // nothing
}

TrackerGOTURN::~TrackerGOTURN()
{
    // nothing
}

TrackerGOTURN::Params::Params()
{
    modelTxt = "goturn.prototxt";
    modelBin = "goturn.caffemodel";
}

#ifdef HAVE_OPENCV_DNN

class TrackerGOTURNImpl : public TrackerGOTURN
{
public:
    TrackerGOTURNImpl(const TrackerGOTURN::Params &parameters)
        : params(parameters)
    {
        // Load GOTURN architecture from *.prototxt and pretrained weights from *.caffemodel
        net = dnn::readNetFromCaffe(params.modelTxt, params.modelBin);
        CV_Assert(!net.empty());
    }

    void init(InputArray image, const Rect& boundingBox) CV_OVERRIDE;
    bool update(InputArray image, Rect& boundingBox) CV_OVERRIDE;

    void setBoudingBox(Rect boundingBox)
    {
        if (image_.empty())
            CV_Error(Error::StsInternal, "Set image first");
        boundingBox_ = boundingBox & Rect(Point(0, 0), image_.size());
    }

    TrackerGOTURN::Params params;

    dnn::Net net;
    Rect boundingBox_;
    Mat image_;
};

void TrackerGOTURNImpl::init(InputArray image, const Rect& boundingBox)
{
    image_ = image.getMat().clone();
    setBoudingBox(boundingBox);
}

bool TrackerGOTURNImpl::update(InputArray image, Rect& boundingBox)
{
    int INPUT_SIZE = 227;
    //Using prevFrame & prevBB from model and curFrame GOTURN calculating curBB
    InputArray curFrame = image;
    Mat prevFrame = image_;
    Rect2d prevBB = boundingBox_;
    Rect curBB;

    float padTargetPatch = 2.0;
    Rect2f searchPatchRect, targetPatchRect;
    Point2f currCenter, prevCenter;
    Mat prevFramePadded, curFramePadded;
    Mat searchPatch, targetPatch;

    prevCenter.x = (float)(prevBB.x + prevBB.width / 2);
    prevCenter.y = (float)(prevBB.y + prevBB.height / 2);

    targetPatchRect.width = (float)(prevBB.width*padTargetPatch);
    targetPatchRect.height = (float)(prevBB.height*padTargetPatch);
    targetPatchRect.x = (float)(prevCenter.x - prevBB.width*padTargetPatch / 2.0 + targetPatchRect.width);
    targetPatchRect.y = (float)(prevCenter.y - prevBB.height*padTargetPatch / 2.0 + targetPatchRect.height);

    targetPatchRect.width = std::min(targetPatchRect.width, (float)prevFrame.cols);
    targetPatchRect.height = std::min(targetPatchRect.height, (float)prevFrame.rows);
    targetPatchRect.x = std::max(-prevFrame.cols * 0.5f, std::min(targetPatchRect.x, prevFrame.cols * 1.5f));
    targetPatchRect.y = std::max(-prevFrame.rows * 0.5f, std::min(targetPatchRect.y, prevFrame.rows * 1.5f));

    copyMakeBorder(prevFrame, prevFramePadded, (int)targetPatchRect.height, (int)targetPatchRect.height, (int)targetPatchRect.width, (int)targetPatchRect.width, BORDER_REPLICATE);
    targetPatch = prevFramePadded(targetPatchRect).clone();

    copyMakeBorder(curFrame, curFramePadded, (int)targetPatchRect.height, (int)targetPatchRect.height, (int)targetPatchRect.width, (int)targetPatchRect.width, BORDER_REPLICATE);
    searchPatch = curFramePadded(targetPatchRect).clone();

    // Preprocess
    // Resize
    resize(targetPatch, targetPatch, Size(INPUT_SIZE, INPUT_SIZE), 0, 0, INTER_LINEAR_EXACT);
    resize(searchPatch, searchPatch, Size(INPUT_SIZE, INPUT_SIZE), 0, 0, INTER_LINEAR_EXACT);

    // Convert to Float type and subtract mean
    Mat targetBlob = dnn::blobFromImage(targetPatch, 1.0f, Size(), Scalar::all(128), false);
    Mat searchBlob = dnn::blobFromImage(searchPatch, 1.0f, Size(), Scalar::all(128), false);

    net.setInput(targetBlob, "data1");
    net.setInput(searchBlob, "data2");

    Mat resMat = net.forward("scale").reshape(1, 1);

    curBB.x = cvRound(targetPatchRect.x + (resMat.at<float>(0) * targetPatchRect.width / INPUT_SIZE) - targetPatchRect.width);
    curBB.y = cvRound(targetPatchRect.y + (resMat.at<float>(1) * targetPatchRect.height / INPUT_SIZE) - targetPatchRect.height);
    curBB.width = cvRound((resMat.at<float>(2) - resMat.at<float>(0)) * targetPatchRect.width / INPUT_SIZE);
    curBB.height = cvRound((resMat.at<float>(3) - resMat.at<float>(1)) * targetPatchRect.height / INPUT_SIZE);

    // Predicted BB
    boundingBox = curBB & Rect(Point(0, 0), image_.size());

    // Set new model image and BB from current frame
    image_ = image.getMat().clone();
    setBoudingBox(curBB);
    return true;
}

Ptr<TrackerGOTURN> TrackerGOTURN::create(const TrackerGOTURN::Params& parameters)
{
    return makePtr<TrackerGOTURNImpl>(parameters);
}

#else // OPENCV_HAVE_DNN
Ptr<TrackerGOTURN> TrackerGOTURN::create(const TrackerGOTURN::Params& parameters)
{
    (void)(parameters);
    CV_Error(cv::Error::StsNotImplemented, "to use GOTURN, the tracking module needs to be built with opencv_dnn !");
}
#endif // OPENCV_HAVE_DNN

}} // namespace
