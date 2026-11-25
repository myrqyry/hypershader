// Real libobs picker against a disposable installed shader directory.
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include "../src/ui/shader_catalog_watch.hpp"
#include "../src/ui/shader_catalog_refresh.hpp"
#include "../src/ui/shader_catalog.hpp"
namespace fs = std::filesystem;
static bool has(obs_properties_t *p, const char *key, const std::string &name) {
    auto *list = obs_properties_get(p, key);
    if (!list) return false;
    for (size_t i = 0; i < obs_property_list_item_count(list); ++i)
        if (std::string(obs_property_list_item_name(list,i)).find(name) != std::string::npos)
            return true;
    return false;
}
static bool check(bool ok, const char *name) {
    std::cout << "LIVE_CATALOG " << name << " PASS=" << ok << std::endl;
    return ok;
}
static void writeShader(const fs::path &p, const char *label) {
    std::ofstream(p) << "// @label \"" << label << "\"\n";
}
static bool waitFor(const std::function<bool()> &condition) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 3000) {
        QCoreApplication::processEvents();
        if (condition()) return true;
        QThread::msleep(10);
    }
    return false;
}
static void propertiesChanged(void *data, calldata_t *) {
    ++*static_cast<int *>(data);
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (argc != 3 || !obs_startup("en-US",nullptr,nullptr)) return 2;
    obs_video_info v{};
    v.graphics_module="libobs-opengl"; v.fps_num=30; v.fps_den=1;
    v.base_width=240; v.base_height=136; v.output_width=240; v.output_height=136;
    v.output_format=VIDEO_FORMAT_RGBA; v.scale_type=OBS_SCALE_DISABLE;
    if (obs_reset_video(&v)!=OBS_VIDEO_SUCCESS) return 3;
    obs_module_t *module=nullptr;
    if (obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS ||
        !obs_init_module(module)) return 4;
    auto *settings=obs_data_create();
    obs_data_set_string(settings,"shader_category","all");
    auto *source=obs_source_create("hypershader_filter","Catalog probe",settings,nullptr);
    if (!source) return 5;
    auto *p=obs_source_properties(source);
    bool good=check(p && !has(p,"preset","Catalog Probe Alpha"),"initial");
    obs_properties_destroy(p);
    const auto root=fs::path(argv[2])/"shaders"/"root";
    const auto collection=root/"catalog-probe";
    fs::create_directory(collection);
    const auto alpha=collection/"alpha.effect";
    const auto beta=collection/"beta.effect";
    writeShader(alpha,"Catalog Probe Alpha");
    p=obs_source_properties(source);
    good &= check(has(p,"preset","Catalog Probe Alpha") && has(p,"shader_category","Catalog Probe"),"add_and_collection");
    obs_properties_destroy(p);
    fs::rename(alpha,beta);
    writeShader(beta,"Catalog Probe Beta");
    p=obs_source_properties(source);
    good &= check(!has(p,"preset","Catalog Probe Alpha") && has(p,"preset","Catalog Probe Beta"),"rename_and_label");
    obs_properties_destroy(p);
    fs::remove(beta); fs::remove(collection);
    p=obs_source_properties(source);
    good &= check(!has(p,"preset","Catalog Probe Beta") && !has(p,"shader_category","Catalog Probe"),"delete");
    obs_properties_destroy(p);
    // Keep a real selected built-in throughout catalog changes. Its ID must
    // remain stable regardless of other files appearing or disappearing.
    p=obs_source_properties(source);
    auto *picker=obs_properties_get(p,"preset");
    long long selectedId=0;
    for (size_t i=0; i<obs_property_list_item_count(picker); ++i) {
        if (std::string(obs_property_list_item_name(picker,i)).find("Spectral Garden")!=std::string::npos)
            selectedId=obs_property_list_item_int(picker,i);
    }
    obs_properties_destroy(p);
    const auto selectedPath=(root/"source.spectral_garden.effect").string();
    auto *active=obs_source_get_settings(source);
    obs_data_set_string(active,"shader_file",selectedPath.c_str());
    obs_data_set_int(active,"preset",selectedId);
    obs_data_set_int(active,"preset_schema",2);
    obs_data_release(active);
    good &= check(selectedId!=0 && fs::is_regular_file(selectedPath),"selected_builtin");
    int signalsReceived=0, watchEvents=0;
    auto *handler=obs_source_get_signal_handler(source);
    signal_handler_connect(handler,"update_properties",propertiesChanged,&signalsReceived);
    const auto folder=QString::fromStdString((fs::path(argv[2])/"shaders").string());
    ShaderCatalogWatch watch(folder,[&]{
        ++watchEvents;
        refreshHyperShaderProperties();
    });
    auto verifyChange=[&](const char *name,const char *oldLabel,const char *newLabel) {
        int expected=watchEvents+1, expectedSignal=signalsReceived+1;
        // Caller changes disk before processing Qt events.
        bool notified=waitFor([&]{return watchEvents>=expected && signalsReceived>=expectedSignal;});
        auto *props=obs_source_properties(source);
        bool native=(!oldLabel || !has(props,"preset",oldLabel)) &&
                    (!newLabel || has(props,"preset",newLabel));
        auto *choices=obs_properties_get(props,"preset");
        bool selectedPresent=false;
        for(size_t i=0;i<obs_property_list_item_count(choices);++i)
            selectedPresent |= obs_property_list_item_int(choices,i)==selectedId;
        obs_properties_destroy(props);
        bool oldFound=false,newFound=false;
        for (const auto &entry:discoverHyperShaderFiles(folder)) {
            auto label=shaderCatalogDisplayLabel(entry.path,entry.category);
            if(oldLabel && label.contains(oldLabel)) oldFound=true;
            if(newLabel && label.contains(newLabel)) newFound=true;
        }
        return check(notified && native && selectedPresent && !oldFound && (!newLabel || newFound),name);
    };
    fs::create_directory(collection);
    writeShader(alpha,"Catalog Watch Alpha");
    good &= verifyChange("watch_add",nullptr,"Catalog Watch Alpha");
    writeShader(alpha,"Catalog Watch Edited");
    good &= verifyChange("watch_edit","Catalog Watch Alpha","Catalog Watch Edited");
    // Atomic save replaces the inode; a second edit must still be observed.
    writeShader(beta,"Catalog Watch Atomic");
    fs::rename(beta,alpha);
    good &= verifyChange("watch_atomic","Catalog Watch Edited","Catalog Watch Atomic");
    writeShader(alpha,"Catalog Watch Rearmed");
    good &= verifyChange("watch_rearm","Catalog Watch Atomic","Catalog Watch Rearmed");
    fs::rename(alpha,beta);
    writeShader(beta,"Catalog Watch Renamed");
    good &= verifyChange("watch_rename","Catalog Watch Rearmed","Catalog Watch Renamed");
    fs::remove(beta); fs::remove(collection);
    good &= verifyChange("watch_delete","Catalog Watch Renamed",nullptr);
    fs::create_directories(root/"tests");
    writeShader(root/"tests"/"probe.effect","Catalog Hidden Fixture");
    good &= verifyChange("exclude_fixture","Catalog Hidden Fixture",nullptr);
    fs::remove(root/"tests"/"probe.effect"); fs::remove(root/"tests");
    // Replacing the entire installed folder must not fall back to stale data.
    const auto shaderRoot=root.parent_path();
    const auto moved=shaderRoot.parent_path()/"moved-shaders";
    int next=watchEvents+1;
    fs::rename(shaderRoot,moved);
    bool notified=waitFor([&]{return watchEvents>=next;});
    p=obs_source_properties(source);
    good &= check(notified && !has(p,"preset","Spectral Garden") &&
                  discoverHyperShaderFiles(folder).isEmpty(),"watch_folder_removed");
    obs_properties_destroy(p);
    fs::rename(moved,shaderRoot);
    good &= verifyChange("watch_folder_restored",nullptr,"Spectral Garden");
    auto *saved=obs_source_get_settings(source);
    good &= check(std::string(obs_data_get_string(saved,"shader_file"))==selectedPath &&
                   obs_data_get_int(saved,"preset")==selectedId &&
                   std::string(obs_data_get_string(saved,"shader_category"))=="all",
                   "settings_unchanged");
    obs_data_release(saved);
    watch.stop();
    signal_handler_disconnect(handler,"update_properties",propertiesChanged,&signalsReceived);
    obs_source_release(source); obs_data_release(settings);
    obs_shutdown();
    return good?0:1;
}
