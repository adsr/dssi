/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* less_trivial_synth_qt_gui.h

   Disposable Hosted Soft Synth API
   Constructed by Chris Cannam and Steve Harris

   This is an example Qt GUI for an example DSSI synth plugin.

   This example file is in the public domain.
*/

#ifndef _LESS_TRIVIAL_SYNTH_QT_GUI_H_INCLUDED_
#define _LESS_TRIVIAL_SYNTH_QT_GUI_H_INCLUDED_

#include <qframe.h>
#include <qdial.h>
#include <qlabel.h>
#include <qlayout.h>

extern "C" {
#include <lo.h>
}

class SynthGUI : public QFrame
{
    Q_OBJECT

public:
    SynthGUI(char *host, char *port, char *path, QWidget *w = 0);
    virtual ~SynthGUI();

public slots:
    void setTuning (float hz);
    void setAttack (float sec);
    void setDecay  (float sec);
    void setSustain(float percent);
    void setRelease(float sec);
    void setTimbre (float val);

protected slots:
    void tuningChanged (int);
    void attackChanged (int);
    void decayChanged  (int);
    void sustainChanged(int);
    void releaseChanged(int);
    void timbreChanged (int);
    void test();

protected:
    QDial *m_tuning;
    QDial *m_attack;
    QDial *m_decay;
    QDial *m_sustain;
    QDial *m_release;
    QDial *m_timbre;

    QLabel *m_tuningLabel;
    QLabel *m_attackLabel;
    QLabel *m_decayLabel;
    QLabel *m_sustainLabel;
    QLabel *m_releaseLabel;
    QLabel *m_timbreLabel;

    lo_address m_host;
    QString m_path;

    bool m_suppressHostUpdate;
};


#endif
