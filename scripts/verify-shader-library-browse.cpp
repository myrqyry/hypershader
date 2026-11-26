#include "../src/ui/shader_catalog.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <iostream>

static bool makeFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write("// shader\n") > 0;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    QTemporaryDir temp;
    if (!temp.isValid())
        return 3;
    QDir d(temp.path());
    if (!d.mkpath("data/shaders/root/rikiya") ||
        !d.mkpath("data/shaders/root/visual-experiments") ||
        !d.mkpath("data/shaders/builder-applied") ||
        !d.mkpath("data/quarantined_shaders/root"))
        return 4;
    const QString base=d.filePath("data/shaders");
    if (!makeFile(base+"/loose.effect") ||
        !makeFile(base+"/root/portrait.effect") ||
        !makeFile(base+"/root/rikiya/rikiya_20250819.effect") ||
        !makeFile(base+"/root/rikiya/rikiya_20260713.effect") ||
        !makeFile(base+"/root/visual-experiments/space.frag") ||
        !makeFile(base+"/builder-applied/revision.effect") ||
        !makeFile(d.filePath("data/quarantined_shaders/root/unsafe.effect")) ||
        !makeFile(base+"/root/ignored.txt"))
        return 5;

    const auto entries=discover_hypershader_files(base);
    int rikiya=0,other=0,quarantined=0;
    bool filenameSearch=false, dateSearch=false, categorySearch=false;
    for (const auto &entry : entries) {
        if (entry.category=="Rikiya Daily Practice") ++rikiya;
        if (entry.category=="Collection / visual-experiments") ++other;
        if (entry.category=="Quarantined") ++quarantined;
        if (shaderCatalogMatches(entry,"20250819",{})) dateSearch=true;
        if (shaderCatalogMatches(entry,"20260713",{})) filenameSearch=true;
        if (entry.category=="Rikiya Daily Practice" &&
            shaderCatalogMatches(entry,"rikiya","Rikiya Daily Practice")) categorySearch=true;
    }
    const bool ignored = !shaderCatalogMatches(
        {base+"/root/rikiya/rikiya_20250819.effect","Rikiya Daily Practice"},
        "20250819","Library");
    const bool displayedRoles=
        shaderCatalogDisplayLabel(base+"/root/rikiya/rikiya_20250819.effect",
                                  "Rikiya Daily Practice")=="Filter / Rikiya / 20250819" &&
        shaderCatalogDisplayLabel(base+"/root/portrait.effect","Library")=="Filter / Portrait";
    bool passes=displayedRoles && entries.size()==7 && rikiya==2 && other==1 &&
                quarantined==1 && dateSearch && filenameSearch && categorySearch &&
                ignored && discover_hypershader_files({}).isEmpty();
    // Only if PR #25 files are present in working tree, require all 56 there.
    const auto source=discover_hypershader_files(QString::fromLocal8Bit(argv[1]));
    int sourceRikiya=0;
    for (const auto &entry : source)
        if (entry.category=="Rikiya Daily Practice")++sourceRikiya;
    passes &= sourceRikiya==56;
    std::cout<<"SHADER_LIBRARY_BROWSE total="<<entries.size()
             <<" nested="<<rikiya<<" other_collection="<<other
             <<" quarantined="<<quarantined
             <<" source_rikiya="<<sourceRikiya
             <<" PASS="<<passes<<"\n";
    return passes?0:1;
}
