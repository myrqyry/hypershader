// Exercise the exact Qt shader catalog used by the Shader Builder dock.
#include "../src/ui/shader_catalog.hpp"
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <iostream>

static bool makeFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))return false;
    return f.write("// test shader\n") > 0;
}

int main(int argc,char **argv) {
    if(argc!=3)return 2;
    QTemporaryDir temp;
    if(!temp.isValid())return 3;
    QDir test(temp.path());
    if(!test.mkpath("data/shaders/root") ||
       !test.mkpath("data/shaders/builder-applied") ||
       !test.mkpath("data/quarantined_shaders/root"))return 4;
    const QString shaders=test.filePath("data/shaders");
    if(!makeFile(shaders+"/loose.effect") ||
       !makeFile(shaders+"/root/portrait.effect") ||
       !makeFile(shaders+"/root/source.ambient.effect") ||
       !makeFile(shaders+"/root/transition.wipe.effect") ||
       !makeFile(shaders+"/builder-applied/revision.effect") ||
       !makeFile(test.filePath("data/quarantined_shaders/root/unsafe.frag")) ||
       !makeFile(shaders+"/root/notes.txt"))return 5;

    // An author's header title wins in the same Qt manager used by OBS.
    {
        QFile author(shaders+"/root/portrait.effect");
        if(!author.open(QIODevice::WriteOnly | QIODevice::Truncate))return 6;
        author.write("// @label \"Soft Portrait Lighting\"\n");
    }
    const auto found=discover_hypershader_files(shaders);
    bool loose=false,library=false,applied=false,quarantined=false,txt=false,
         namedSource=false,namedTransition=false,namedFilter=false;
    for(const auto &e:found) {
        const QString filename=QFileInfo(e.path).fileName();
        loose|=filename=="loose.effect"&&e.category=="Loose";
        library|=filename=="portrait.effect"&&e.category=="Library";
        namedFilter|=filename=="portrait.effect"&&
            shaderCatalogDisplayLabel(e.path,e.category)=="Filter / Soft Portrait Lighting";
        namedSource|=filename=="source.ambient.effect"&&
            shaderCatalogDisplayLabel(e.path,e.category)=="Source / Ambient";
        namedTransition|=filename=="transition.wipe.effect"&&
            shaderCatalogDisplayLabel(e.path,e.category)=="Transition / Wipe";
        applied|=filename=="revision.effect"&&e.category=="Applied";
        quarantined|=filename=="unsafe.frag"&&e.category=="Quarantined";
        txt|=filename=="notes.txt";
    }
    const auto empty=discover_hypershader_files(QString());
    const auto realSource=discover_hypershader_files(QString::fromLocal8Bit(argv[1]));
    const auto installed=discover_hypershader_files(QString::fromLocal8Bit(argv[2]));
    bool sourcePFrame=false,installedPFrame=false;
    for(const auto &e:realSource)
        sourcePFrame|=e.category=="Library" &&
            QFileInfo(e.path).fileName()=="pframe_drift.effect";
    for(const auto &e:installed)
        installedPFrame|=e.category=="Library" &&
            QFileInfo(e.path).fileName()=="pframe_drift.effect";
    const bool pass=found.size()==6&&loose&&library&&applied&&quarantined&&
        namedFilter&&namedSource&&namedTransition&&
        shaderCatalogMatches({shaders+"/root/portrait.effect","Library"},
                             "soft portrait",QString())&&
        shaderCatalogMatches({shaders+"/root/portrait.effect","Library"},
                             "Filter / ",QString())&&
        !txt&&empty.isEmpty()&&sourcePFrame&&installedPFrame;
    std::cout<<"SHADER_MANAGER_CATALOG test_files="<<found.size()
             <<" library="<<library<<" applied="<<applied
             <<" quarantined="<<quarantined<<" source_pframe="<<sourcePFrame
             <<" installed_pframe="<<installedPFrame
             <<" PASS="<<pass<<"\n";
    return pass?0:1;
}
