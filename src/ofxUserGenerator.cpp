#include "ofxUserGenerator.h"
#include "ofxOpenNIMacros.h"
#include "ofxTrackedUser.h"

// CALLBACKS
// =============================================================================
// Callback: New user was detected
void XN_CALLBACK_TYPE User_NewUser(
								   xn::UserGenerator& rGenerator
								   ,XnUserID nID
								   ,void* pCookie
								   )
{
	printf("New User %d\n", nID);
	ofxUserGenerator* user = static_cast<ofxUserGenerator*>(pCookie);
	if(user->needsPoseForCalibration()) {
		user->startPoseDetection(nID);
	}
	else {
		user->requestCalibration(nID);	
	}
}

// Callback: An existing user was lost
void XN_CALLBACK_TYPE User_LostUser(
									xn::UserGenerator& rGenerator
									,XnUserID nID
									,void* pCookie
									)
{
	printf("Lost user %d\n", nID);
}

// Callback: Detected a pose
void XN_CALLBACK_TYPE UserPose_PoseDetected(
											xn::PoseDetectionCapability& rCapability
											,const XnChar* strPose
											,XnUserID nID
											,void* pCookie
											)
{
	ofxUserGenerator* user = static_cast<ofxUserGenerator*>(pCookie);
	printf("Pose %s detected for user %d\n", strPose, nID);
	user->stopPoseDetection(nID);
	user->requestCalibration(nID);
}



void XN_CALLBACK_TYPE UserCalibration_CalibrationStart(
													   xn::SkeletonCapability& capability
													   ,XnUserID nID
													   ,void* pCookie
													   )
{
	printf("Calibration started for user %d\n", nID);
}


void XN_CALLBACK_TYPE UserCalibration_CalibrationEnd(
													 xn::SkeletonCapability& rCapability
													 ,XnUserID nID
													 ,XnBool bSuccess
													 ,void* pCookie
													 )
{
	ofxUserGenerator* user = static_cast<ofxUserGenerator*>(pCookie);
	if(bSuccess) {
		printf("+++++++++++++++++++++++ Succesfully tracked user: %d\n", nID);
		user->startTracking(nID);
	}
	else {
		if(user->needsPoseForCalibration()) {
			user->startPoseDetection(nID);
		}
		else {
			user->requestCalibration(nID);	
		}
	}
}

XnFloat Colors[][3] =
{
	{0,1,1},
	{0,0,1},
	{0,1,0},
	{1,1,0},
	{1,0,0},
	{1,.5,0},
	{.5,1,0},
	{0,.5,1},
	{.5,0,1},
	{1,1,.5},
	{1,1,1}
};
XnUInt32 nColors = 10;

// OFXUSERGENERATOR
// =============================================================================
ofxUserGenerator::ofxUserGenerator() 
:needs_pose(false)
,num_users(15)
,is_initialized(false)
{
	found_user = false;
}


//----------------------------------------
void ofxUserGenerator::startPoseDetection(XnUserID nID) {
	printf("Start pose detection: %d +++++++++++++++++++++++++++++\n", nID);
	user_generator.GetPoseDetectionCap().StartPoseDetection(calibration_pose, nID);
}


//----------------------------------------
void ofxUserGenerator::stopPoseDetection(XnUserID nID) {
	user_generator.GetPoseDetectionCap().StopPoseDetection(nID);
}


//----------------------------------------
void ofxUserGenerator::requestCalibration(XnUserID nID) {
	user_generator.GetSkeletonCap().RequestCalibration(nID, TRUE);
}


// Setup the user generator.
//----------------------------------------
bool ofxUserGenerator::setup(ofxOpenNIContext* pContext, ofxDepthGenerator* pDepthGenerator) {
	if(!pContext->isInitialized()) {
		return false;
	}
	
	depth_generator = pDepthGenerator;
	context			= pContext;
	XnStatus result = XN_STATUS_OK;
	
	// check if the USER generator exists.
	result = context->getXnContext().FindExistingNode(XN_NODE_TYPE_USER, user_generator);
	SHOW_RC(result, "Find user generator");
	if(result != XN_STATUS_OK) {
		// create user generator.
		result = user_generator.Create(context->getXnContext());
		SHOW_RC(result, "Create user generator");
		if(result != XN_STATUS_OK) {
			return false;
		}
	}	
	
	// register user callbacks/
	XnCallbackHandle user_cb_handle;
	user_generator.RegisterUserCallbacks(
										 User_NewUser
										 ,User_LostUser
										 ,this
										 ,user_cb_handle
										 );
	
	XnCallbackHandle calibration_cb_handle;
	user_generator.GetSkeletonCap().RegisterCalibrationCallbacks(
																 UserCalibration_CalibrationStart
																 ,UserCalibration_CalibrationEnd
																 ,this
																 ,calibration_cb_handle
																 );
	
	// check if we need to pose for calibration
	if(user_generator.GetSkeletonCap().NeedPoseForCalibration()) {
		needs_pose = true;
		if(!user_generator.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION)) {
			printf("Pose required, but not supported!\n");
			return false;
		}
		XnCallbackHandle user_pose_cb_handle;
		user_generator.GetPoseDetectionCap().RegisterToPoseCallbacks(
																	 UserPose_PoseDetected
																	 ,NULL
																	 ,this
																	 ,user_pose_cb_handle
																	 );
		user_generator.GetSkeletonCap().GetCalibrationPose(calibration_pose);
	}
	
	user_generator.GetSkeletonCap().SetSkeletonProfile(XN_SKEL_PROFILE_ALL);
	
	// pre-generate the tracked users.
	tracked_users.reserve(num_users);
	for(int i = 0; i < num_users; ++i) {
		ofxTrackedUser* tracked_user = new ofxTrackedUser(this, pDepthGenerator);
		tracked_users.push_back(tracked_user);
	}
	is_initialized = true;
	
	user_generator.StartGenerating();
	
	scene_texture.allocate(640, 480, GL_RGBA);		
	scene_pixels = new unsigned char[640 * 480 * 4];
	memset(scene_pixels, 0, 640 * 480 * 4 * sizeof(unsigned char));
	
	return true;
}


// Draw all the found users.
//----------------------------------------
void ofxUserGenerator::drawUsers() {
	for(int i = 0;  i < found_users; ++i) {
		drawUser(i);
	}
}


// Draw a specific user (start counting at 0)
//----------------------------------------
void ofxUserGenerator::drawUser(int nUserNum) {
	if(nUserNum > tracked_users.size()-1)
		return;
	tracked_users.at(nUserNum)->updateBonePositions();
	tracked_users.at(nUserNum)->debugDraw();
}

// Draw the sceneanalyzer 
//----------------------------------------
void ofxUserGenerator::drawScene(){
	// get meta-data
	xn::DepthMetaData dmd;
	xn::SceneMetaData smd;
	depth_generator->getXnDepthGenerator().GetMetaData(dmd);	
	user_generator.GetUserPixels(0, smd);
	
	
	// get the pixels
	const XnDepthPixel* depth = dmd.Data();
	const XnLabel* pLabels = smd.Data();
	
	XN_ASSERT(depth);
	
	if (dmd.FrameID() == 0){
		return;
	}
	if (dmd.PixelFormat() == XN_PIXEL_FORMAT_RGB24) {
		printf("its in yuv\n");
	}
	
	// copy depth into texture-map

	for (XnUInt16 y = dmd.YOffset(); y < dmd.YRes() + dmd.YOffset(); y++) {
		unsigned char * texture = (unsigned char*)scene_pixels + y * dmd.XRes() * 4 + dmd.XOffset()*4;
		for (XnUInt16 x = 0; x < dmd.XRes(); x++, depth++, texture+=4, pLabels++){
			
			XnUInt8 red = 0;
			XnUInt8 green = 0;
			XnUInt8 blue = 0;
			XnUInt8 alpha = 255;
			
			XnLabel label = *pLabels;
			XnUInt32 nColorID = label % nColors;
			
			if (label == 0){
				nColorID = nColors;
			}		
			
			if (*depth != 0){	
				float d =  *depth/10.0;
				green = d * Colors[nColorID][0]; 
				red = d * Colors[nColorID][1];
				blue = d * Colors[nColorID][2];
			}
					
			texture[0] = red;
			texture[1] = green;
			texture[2] = blue;
			
			if (*depth == 0)
				texture[3] = 0;
			else
				texture[3] = alpha;
		}	
	}		
	
	scene_texture.loadData((unsigned char *)scene_pixels,dmd.XRes(), dmd.YRes(), GL_RGBA);	
	scene_texture.draw(0,0,640,480);
}

// Get a ref to the xn::UserGenerator object.
//----------------------------------------
xn::UserGenerator& ofxUserGenerator::getXnUserGenerator() {
	return user_generator;
}


// Get a tracked user.
//----------------------------------------
ofxTrackedUser* ofxUserGenerator::getTrackedUser(int nUserNum) {
	
	ofxTrackedUser* found_user = NULL;
	try {
		found_user = tracked_users.at(nUserNum);
	}
	catch( std::out_of_range& rEx) {
		return NULL;
	}
	return found_user;
}

std::vector<ofxTrackedUser*> ofxUserGenerator::getTrackedUsers() {
	std::vector<ofxTrackedUser*> found;
	std::vector<ofxTrackedUser*>::iterator it = tracked_users.begin();
	while(it != tracked_users.end()) {
		if( (*it)->is_tracked) {
			found.push_back(*it);
		}			
		++it;
	}
	return found;
}


// Update the tracked users, should be called each frame
//----------------------------------------
void ofxUserGenerator::update() {
	if(!is_initialized) {
		return;
	}
	
	// unset
	std::vector<ofxTrackedUser*>::iterator it = tracked_users.begin();
	while(it != tracked_users.end()) {
		(*it)->is_tracked = false;
		++it;
	}
	
	found_user = false;
	XnUserID* users = new XnUserID[num_users];
	user_generator.GetUsers(users, found_users);
	for(int i = 0; i < found_users; ++i) {
		if(user_generator.GetSkeletonCap().IsTracking(users[i])) {	
			found_user = true;
			tracked_users.at(i)->is_tracked = true;
			tracked_users.at(i)->id = users[i];
			tracked_users.at(i)->updateBonePositions();
		}
	}
	found_users = num_users;
	delete[] users;
}

// Draw all users.
//----------------------------------------
void ofxUserGenerator::draw() {
	if(!is_initialized) {
		return;
	}
	glColor3f(1.0, 1.0, 1.0);
	drawScene();
	
	drawUsers();
	if(!found_user) {
		glColor3f(1.0, 0, 0);
	}
	else {
		glColor3f(0.0, 1, 0);
	}
	ofCircle(10,10,10);
}


//----------------------------------------
void ofxUserGenerator::startTracking(XnUserID nID) {
	user_generator.GetSkeletonCap().StartTracking(nID);
}

//----------------------------------------
bool ofxUserGenerator::needsPoseForCalibration() {
	return needs_pose;
}