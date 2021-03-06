#include "DBCDeclarations.h"
#include "SharedDefines.h"
#include "ProgressBar.h"
#include <stdio.h>
#include <stdlib.h>
#include "DBCExport.h"
#include "Common.h"
#include <iostream>
#include <fstream>
#include "Database\Database.h"

static bool ReadDBCBuildFileText(const std::string& dbc_path, char const* localeName, std::string& text)
{
    std::string filename = dbc_path + "component.wow-" + localeName + ".txt";

    if (FILE* file = fopen(filename.c_str(), "rb"))
    {
        char buf[100];
        fread(buf, 1, 100 - 1, file);
        fclose(file);

        text = &buf[0];
        return true;
    }
    else
        return false;
}

static uint32 ReadDBCBuild(const std::string& dbc_path, LocaleNameStr const* localeNameStr = nullptr)
{
    std::string text;

    if (!localeNameStr)
    {
        for (LocaleNameStr const* itr = &fullLocaleNameList[0]; itr->name; ++itr)
        {
            if (ReadDBCBuildFileText(dbc_path, itr->name, text))
            {
                localeNameStr = itr;
                break;
            }
        }
    }
    else
        ReadDBCBuildFileText(dbc_path, localeNameStr->name, text);

    if (text.empty())
        return 0;

    size_t pos = text.find("version=\"");
    size_t pos1 = pos + strlen("version=\"");
    size_t pos2 = text.find("\"", pos1);
    if (pos == text.npos || pos2 == text.npos || pos1 >= pos2)
        return 0;

    std::string build_str = text.substr(pos1, pos2 - pos1);

    int build = atoi(build_str.c_str());
    if (build <= 0)
        return 0;

    return build;
}

struct LocalData
{
    LocalData(uint32 build)
        : main_build(build), availableDbcLocales(0xFFFFFFFF), checkedDbcLocaleBuilds(0) {}

    uint32 main_build;

    // bitmasks for index of fullLocaleNameList
    uint32 availableDbcLocales;
    uint32 checkedDbcLocaleBuilds;
};

#if COMPILER == COMPILER_MICROSOFT
#  define SIZEFMTD "%Iu"
#else
#  define SIZEFMTD "%zu"
#endif

#  define snprintf _snprintf

template<class T>
inline void LoadDBCWotlk(LocalData& localeData, BarGoLink& bar, StoreProblemList& errlist, DBCStorage<T>& storage, const std::string& dbc_path, const std::string& filename)
{
    // compatibility format and C++ structure sizes
    //MANGOS_ASSERT(DBCFileLoader::GetFormatRecordSize(storage.GetFormat()) == sizeof(T) || LoadDBC_assert_print(DBCFileLoader::GetFormatRecordSize(storage.GetFormat()), sizeof(T), filename));

    std::string dbc_filename = dbc_path + filename;
    if (storage.Load(dbc_filename.c_str()))
    {
        bar.step();
        for (uint8 i = 0; fullLocaleNameList[i].name; ++i)
        {
            if (!(localeData.availableDbcLocales & (1 << i)))
                continue;

            LocaleNameStr const* localStr = &fullLocaleNameList[i];

            std::string dbc_dir_loc = dbc_path + localStr->name + "/";

            if (!(localeData.checkedDbcLocaleBuilds & (1 << i)))
            {
                localeData.checkedDbcLocaleBuilds |= (1 << i); // mark as checked for speedup next checks

                uint32 build_loc = ReadDBCBuild(dbc_dir_loc, localStr);
                if (localeData.main_build != build_loc)
                {
                    localeData.availableDbcLocales &= ~(1 << i); // mark as not available for speedup next checks

                                                                 // exist but wrong build
                    if (build_loc)
                    {
                        std::string dbc_filename_loc = dbc_path + localStr->name + "/" + filename;
                        char buf[200];
                        snprintf(buf, 200, " (exist, but DBC locale subdir %s have DBCs for build %u instead expected build %u, it and other DBC from subdir skipped)", localStr->name, build_loc, localeData.main_build);
                        errlist.push_back(dbc_filename_loc + buf);
                    }

                    continue;
                }
            }

            std::string dbc_filename_loc = dbc_path + localStr->name + "/" + filename;
            if (!storage.LoadStringsFrom(dbc_filename_loc.c_str()))
                localeData.availableDbcLocales &= ~(1 << i);// mark as not available for speedup next checks
        }
    }
    else
    {
        // sort problematic dbc to (1) non compatible and (2) nonexistent
        FILE* f = fopen(dbc_filename.c_str(), "rb");
        if (f)
        {
            char buf[100];
            snprintf(buf, 100, " (exist, but have %u fields instead " SIZEFMTD ") Wrong client version DBC file?", storage.GetFieldCount(), strlen(storage.GetFormat()));
            errlist.push_back(dbc_filename + buf);
            fclose(f);
        }
        else
            errlist.push_back(dbc_filename);
    }
}

template<class T>
inline void LoadDBC(uint32& availableDbcLocales, BarGoLink& bar, StoreProblemList& errlist, DBCStorage<T>& storage, const std::string& dbc_path, const std::string& filename, bool vanilla)
{
    // compatibility format and C++ structure sizes
    //MANGOS_ASSERT(DBCFileLoader::GetFormatRecordSize(storage.GetFormat()) == sizeof(T) || LoadDBC_assert_print(DBCFileLoader::GetFormatRecordSize(storage.GetFormat()), sizeof(T), filename));

    std::string dbc_filename = dbc_path + filename;
    if (storage.Load(dbc_filename.c_str()))
    {
        bar.step();
        if (vanilla)
        {
            for (uint8 i = 0; fullLocaleNameListVanilla[i].name; ++i)
            {
                if (!(availableDbcLocales & (1 << i)))
                    continue;

                std::string dbc_filename_loc = dbc_path + fullLocaleNameListVanilla[i].name + "/" + filename;
                if (!storage.LoadStringsFrom(dbc_filename_loc.c_str()))
                    availableDbcLocales &= ~(1 << i);           // mark as not available for speedup next checks
            }
        }
        {
            for (uint8 i = 0; fullLocaleNameList[i].name; ++i)
            {
                if (!(availableDbcLocales & (1 << i)))
                    continue;

                std::string dbc_filename_loc = dbc_path + fullLocaleNameList[i].name + "/" + filename;
                if (!storage.LoadStringsFrom(dbc_filename_loc.c_str()))
                    availableDbcLocales &= ~(1 << i);           // mark as not available for speedup next checks
            }
        }
    }
    else
    {
        // sort problematic dbc to (1) non compatible and (2) nonexistent
        FILE* f = fopen(dbc_filename.c_str(), "rb");
        if (f)
        {
            char buf[100];
            snprintf(buf, 100, " (exist, but have %u fields instead " SIZEFMTD ") Wrong client version DBC file?", storage.GetFieldCount(), strlen(storage.GetFormat()));
            errlist.push_back(dbc_filename + buf);
            fclose(f);
        }
        else
            errlist.push_back(dbc_filename);
    }
}

int main()
{
    StoreProblemList bad_dbc_files;
    uint32 availableDbcLocales = 0xFFFFFFFF;
    
    printf("This tool converts the needed dbc storages into sql files. \n");
    printf("Please enter the path to the dbc files and the build they are from.\n\n");

    std::string dbcPath;
    printf("Path: ");
    getline(std::cin, dbcPath);
    if (dbcPath.empty())
        dbcPath = "dbc/";

    std::string buildString;
    printf("Build: ");
    getline(std::cin, buildString);
    if (buildString.empty())
        buildString = "5875";

    uint32 build = strtoul(buildString.c_str(), NULL, 0);

    switch (build)
    {
        case 5875:
        {
            {
                printf("\nReading data from DBC files...\n");
                const uint32 DBCFilesCount = 6;
                BarGoLink bar(DBCFilesCount);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sAreaTriggerStore5875, dbcPath, "AreaTrigger.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sTaxiNodeStore5875, dbcPath, "TaxiNodes.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionStore5875, dbcPath, "Faction.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionTemplateStore5875, dbcPath, "FactionTemplate.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSpellStore5875, dbcPath, "Spell.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSkillLineAbilityStore5875, dbcPath, "SkillLineAbility.dbc", true);
            }
            
            ExportAreaTriggers5875(build);
            ExportFactions5875(build);
            ExportSpells5875(build);
            ExportSkillLineAbilities5875(build);
            ExportTaxiNodes5875(build);
            break;
        }
        case 5464:
        {
            {
                printf("\nReading data from DBC files...\n");
                const uint32 DBCFilesCount = 6;
                BarGoLink bar(DBCFilesCount);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sAreaTriggerStore5875, dbcPath, "AreaTrigger.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sTaxiNodeStore5875, dbcPath, "TaxiNodes.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionStore5875, dbcPath, "Faction.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionTemplateStore5875, dbcPath, "FactionTemplate.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSpellStore5464, dbcPath, "Spell.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSkillLineAbilityStore5875, dbcPath, "SkillLineAbility.dbc", true);
            }

            ExportAreaTriggers5875(build);
            ExportFactions5875(build);
            ExportSpells5464(build);
            ExportSkillLineAbilities5875(build);
            ExportTaxiNodes5875(build);
            break;
        }
        case 5302:
        case 5086:
        {
            {
                printf("\nReading data from DBC files...\n");
                const uint32 DBCFilesCount = 6;
                BarGoLink bar(DBCFilesCount);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sAreaTriggerStore5875, dbcPath, "AreaTrigger.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sTaxiNodeStore5302, dbcPath, "TaxiNodes.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionStore5875, dbcPath, "Faction.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionTemplateStore5875, dbcPath, "FactionTemplate.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSpellStore5302, dbcPath, "Spell.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSkillLineAbilityStore5875, dbcPath, "SkillLineAbility.dbc", true);
            }

            ExportAreaTriggers5875(build);
            ExportFactions5875(build);
            ExportSpells5302(build);
            ExportSkillLineAbilities5875(build);
            ExportTaxiNodes5302(build);
            break;
        }
        case 4878:
        {
            {
                printf("\nReading data from DBC files...\n");
                const uint32 DBCFilesCount = 6;
                BarGoLink bar(DBCFilesCount);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sAreaTriggerStore5875, dbcPath, "AreaTrigger.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sTaxiNodeStore5302, dbcPath, "TaxiNodes.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionStore5875, dbcPath, "Faction.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionTemplateStore5875, dbcPath, "FactionTemplate.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSpellStore4878, dbcPath, "Spell.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSkillLineAbilityStore5875, dbcPath, "SkillLineAbility.dbc", true);
            }

            ExportAreaTriggers5875(build);
            ExportFactions5875(build);
            ExportSpells4878(build);
            ExportSkillLineAbilities5875(build);
            ExportTaxiNodes5302(build);
            break;
        }
        case 4695:
        case 4544:
        {
            {
                printf("\nReading data from DBC files...\n");
                const uint32 DBCFilesCount = 6;
                BarGoLink bar(DBCFilesCount);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sAreaTriggerStore5875, dbcPath, "AreaTrigger.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sTaxiNodeStore5302, dbcPath, "TaxiNodes.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionStore5875, dbcPath, "Faction.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionTemplateStore5875, dbcPath, "FactionTemplate.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSpellStore4695, dbcPath, "Spell.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSkillLineAbilityStore5875, dbcPath, "SkillLineAbility.dbc", true);
            }

            ExportAreaTriggers5875(build);
            ExportFactions5875(build);
            ExportSpells4695(build);
            ExportSkillLineAbilities5875(build);
            ExportTaxiNodes5302(build);
            break;
        }
        case 4449:
        case 4375:
        {
            {
                printf("\nReading data from DBC files...\n");
                const uint32 DBCFilesCount = 6;
                BarGoLink bar(DBCFilesCount);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sAreaTriggerStore5875, dbcPath, "AreaTrigger.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sTaxiNodeStore5302, dbcPath, "TaxiNodes.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionStore5875, dbcPath, "Faction.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionTemplateStore5875, dbcPath, "FactionTemplate.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSpellStore4449, dbcPath, "Spell.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSkillLineAbilityStore5875, dbcPath, "SkillLineAbility.dbc", true);
            }

            ExportAreaTriggers5875(build);
            ExportFactions5875(build);
            ExportSpells4449(build);
            ExportSkillLineAbilities5875(build);
            ExportTaxiNodes5302(build);
            break;
        }
        case 4297:
        case 4222:
        {
            {
                printf("\nReading data from DBC files...\n");
                const uint32 DBCFilesCount = 6;
                BarGoLink bar(DBCFilesCount);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sAreaTriggerStore5875, dbcPath, "AreaTrigger.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sTaxiNodeStore5302, dbcPath, "TaxiNodes.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionStore4297, dbcPath, "Faction.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sFactionTemplateStore5875, dbcPath, "FactionTemplate.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSpellStore4297, dbcPath, "Spell.dbc", true);
                LoadDBC(availableDbcLocales, bar, bad_dbc_files, sSkillLineAbilityStore5875, dbcPath, "SkillLineAbility.dbc", true);
            }

            ExportAreaTriggers5875(build);
            ExportFactions4297(build);
            ExportSpells4297(build);
            ExportSkillLineAbilities5875(build);
            ExportTaxiNodes5302(build);
            break;
        }
        default:
        {
            printf("Unsupported build.\n");
        }
    }
    
    printf("Done.\n");
    getchar();
    return 0;
}