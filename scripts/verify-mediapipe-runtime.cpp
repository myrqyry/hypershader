#include "mediapipe/mp_wrapper.h"
#include <obs.h>
#include <util/bmem.h>
#include <cstdio>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cmath>

int main(int argc, char **argv)
{
    if (argc < 2 || !obs_startup("en-US", nullptr, nullptr)) return 2;
    mp_runner_t *runner = mp_create(false, argv[1]);
    if (!runner) { std::puts("MP_RUNTIME_INIT=0"); obs_shutdown(); return 3; }
    std::vector<uint8_t> blank(256 * 256 * 4, 0);
    for (size_t i = 3; i < blank.size(); i += 4) blank[i] = 255;
    // Global backend initialization must not make an unusable second runner valid.
    mp_runner_t *badRunner = mp_create(false, "/tmp/hyper-mediapipe-models-do-not-exist");
    bool missingModelsRejected = badRunner == nullptr;
    if (badRunner) mp_destroy(badRunner);
    bool meshGraph = mp_start_graph(runner, MP_GRAPH_FACE_MESH);
    bool blankProcessed = mp_process_frame(runner, blank.data(), 256, 256, 256*4);
    mp_landmark_list_t blankLandmarks = {};
    bool blankHasLandmarks = mp_get_landmarks(runner, &blankLandmarks);
    mp_landmark_list_t blankStatic{};
    bool blankStaticDetected = mp_process_image(runner,blank.data(),256,256,256*4,&blankStatic);
    if (blankLandmarks.landmarks) bfree(blankLandmarks.landmarks);
    if (blankStatic.landmarks) bfree(blankStatic.landmarks);
    std::printf("MP_RUNTIME_BACKEND=%s mesh_start=%d blank_processed=%d blank_has_landmarks=%d blank_static_detected=%d\n",
        mp_get_backend_name(), meshGraph, blankProcessed, blankHasLandmarks, blankStaticDetected);

    bool poseStarted = mp_start_graph(runner, MP_GRAPH_POSE_ESTIMATION);
    bool poseDisabled = mp_get_current_graph(runner) == MP_GRAPH_NONE;
    bool poseProcessed = mp_process_frame(runner, blank.data(), 256, 256, 256*4);
    bool handStarted = mp_start_graph(runner, MP_GRAPH_HAND_TRACKING);
    bool handProcessed = mp_process_frame(runner, blank.data(), 256, 256, 256*4);
    bool depthStarted = mp_start_graph(runner, MP_GRAPH_DEPTH_ESTIMATION);
    bool depthProcessed = mp_process_frame(runner, blank.data(), 256, 256, 256*4);
    mp_depth_data_t invalidDepth{};
    bool fakeDepth = mp_get_depth_map(runner, &invalidDepth);
    if (invalidDepth.buffer) bfree(invalidDepth.buffer);
    bool cannyStarted = mp_start_graph(runner, MP_GRAPH_CANNY);
    bool cannyProcessed = mp_process_frame(runner, blank.data(), 256, 256, 256*4);
    std::printf("MP_UNSUPPORTED pose=%d/%d hand=%d/%d depth=%d/%d canny=%d/%d\n",
        poseStarted, poseProcessed, handStarted, handProcessed, depthStarted, depthProcessed,
        cannyStarted, cannyProcessed);

    bool privacyGraph=mp_start_graph(runner,MP_GRAPH_PRIVACY);
    bool privacyProcessed=mp_process_frame(runner,blank.data(),256,256,256*4);
    mp_mask_data_t privacyMask{};
    bool privacyMaskAvailable=mp_get_segmentation_mask(runner,&privacyMask);
    bool privacyNoFaceKeepsFrame=privacyMaskAvailable && privacyMask.width==256 &&
        privacyMask.height==256 && privacyMask.buffer &&
        privacyMask.buffer[0]==255 &&
        privacyMask.buffer[128*256+128]==255;
    if(privacyMask.buffer)bfree(privacyMask.buffer);
    std::printf("MP_PRIVACY no_face_keeps_frame=%d\n",privacyNoFaceKeepsFrame);
    int faceLandmarks=0;
    int finiteCoords=0;
    int validCoords=0;
    bool positiveStatic=false;
    float xMin=1e9f,xMax=-1e9f,yMin=1e9f,yMax=-1e9f;
    if (argc > 2) {
        std::ifstream input(argv[2], std::ios::binary);
        std::vector<uint8_t> face(256*256*4);
        if (input.read(reinterpret_cast<char *>(face.data()), face.size())) {
            mp_start_graph(runner, MP_GRAPH_FACE_MESH);
            mp_landmark_list_t staticLandmarks{};
            positiveStatic=mp_process_image(runner,face.data(),256,256,256*4,&staticLandmarks);
            if (staticLandmarks.landmarks) bfree(staticLandmarks.landmarks);
            bool ok = mp_process_frame(runner, face.data(), 256, 256, 256*4);
            mp_landmark_list_t landmarks{};
            if (ok && mp_get_landmarks(runner, &landmarks)) {
                faceLandmarks=landmarks.count;
                for (int i=0;i<landmarks.count;i++){
                    auto &p=landmarks.landmarks[i];
                    if (std::isfinite(p.x) && std::isfinite(p.y)) finiteCoords++;
                    if (p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1) validCoords++;
                    xMin=std::min(xMin,p.x); xMax=std::max(xMax,p.x);
                    yMin=std::min(yMin,p.y); yMax=std::max(yMax,p.y);
                }
            }
            if (landmarks.landmarks) bfree(landmarks.landmarks);
            std::printf("MP_FACE_PROBE input_ok=%d static_detected=%d landmark_count=%d finite=%d bounded=%d x=[%.3f,%.3f] y=[%.3f,%.3f]\n",
                ok,positiveStatic,faceLandmarks,finiteCoords,validCoords,xMin,xMax,yMin,yMax);
        } else std::puts("MP_FACE_PROBE input_missing");
    }
    mp_destroy(runner);
    obs_shutdown();

    bool pass = missingModelsRejected && privacyGraph && privacyProcessed &&
        privacyNoFaceKeepsFrame && meshGraph && blankProcessed && !blankHasLandmarks && !blankStaticDetected
        && !poseStarted && poseDisabled && !poseProcessed && !fakeDepth
        && !handStarted && !handProcessed
        && !depthStarted && !depthProcessed
        && !cannyStarted && !cannyProcessed;
    if (argc > 2) pass &= positiveStatic && faceLandmarks == 468 && finiteCoords == 468 &&
        validCoords >= 400 && xMax-xMin>0.2f && yMax-yMin>0.2f;
    std::printf("MP_RUNNER missing_models_rejected=%d\n",missingModelsRejected);
    std::printf("MP_RUNTIME_PASS=%d\n", pass);
    return pass ? 0 : 1;
}
