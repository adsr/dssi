/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* less_trivial_synth_qt_gui.h

   Disposable Hosted Soft Synth API version 0.1
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

#include <lo.h>

class SynthGUI : public QFrame
{
    Q_OBJECT

public:
    SynthGUI(char *hostUrl, QWidget *w = 0);
    virtual ~SynthGUI();

public slots:
    void setTuning (float hz);
    void setAttack (float sec);
    void setDecay  (float sec);
    void setSustain(float percent);
    void setRelease(float sec);

protected slots:
    void tuningChanged (int);
    void attackChanged (int);
    void decayChanged  (int);
    void sustainChanged(int);
    void releaseChanged(int);

protected:
    QDial *m_tuning;
    QDial *m_attack;
    QDial *m_decay;
    QDial *m_sustain;
    QDial *m_release;

    QLabel *m_tuningLabel;
    QLabel *m_attackLabel;
    QLabel *m_decayLabel;
    QLabel *m_sustainLabel;
    QLabel *m_releaseLabel;

    lo_target m_host;

    bool m_suppressHostUpdate;
};


#endif
