// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Parser regression tool: ScoreTest <file-or-directory> [maxFiles]
#include <juce_core/juce_core.h>
#include "../Source/IO/GPReader.h"

int main (int argc, char** argv)
{
    if (argc < 2) { printf ("usage: ScoreTest <file|dir> [max]\n"); return 1; }
    juce::File f (juce::String::fromUTF8 (argv[1]));
    int maxFiles = argc > 2 ? atoi (argv[2]) : 1000000;
    juce::Array<juce::File> files;
    if (f.isDirectory()) files = f.findChildFiles (juce::File::findFiles, true, "*.gp;*.gpx;*.gp5;*.gp4;*.gp3");
    else files.add (f);
    int ok = 0, fail = 0, n = 0;
    for (auto& file : files)
    {
        if (n++ >= maxFiles) break;
        juce::String err;
        auto s = tc::loadGuitarPro (file, err);
        if (! s) { ++fail; printf ("FAIL %s : %s\n", file.getFullPathName().toRawUTF8(), err.toRawUTF8()); continue; }
        ++ok;
        if (files.size() == 1)
        {
            printf ("%s | %s - %s | tempo %.1f | bars %d | tracks %d\n", s->formatName.c_str(), s->artist.c_str(), s->title.c_str(), s->tempo, (int) s->masterBars.size(), (int) s->tracks.size());
            for (auto& t : s->tracks)
            {
                int notes = 0; for (auto& b : t.bars) for (auto& v : b.voices) for (auto& bt : v.beats) notes += (int) bt.notes.size();
                printf ("  [%s] prog %d fam %s strings %d capo %d notes %d vol %.2f pan %.2f\n", t.name.c_str(), t.program, tc::familyName (t.family), (int) t.tuning.size(), t.capo, notes, t.volume, t.pan);
            }
        }
    }
    printf ("ok %d fail %d\n", ok, fail);
    return 0;
}
