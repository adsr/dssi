/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* less_trivial_synth_qt_gui.h

   DSSI Soft Synth Interface
   Constructed by Chris Cannam and Steve Harris

   This is an example Qt GUI for an example DSSI synth plugin.

   This example file is in the public domain.
*/

#ifndef _LESS_TRIVIAL_SYNTH_QT_GUI_H_INCLUDED_
#define _LESS_TRIVIAL_SYNTH_QT_GUI_H_INCLUDED_

#include <QFrame>
#include <QDial>
#include <QLabel>
#include <QLayout>

extern "C" {
#include <lo/lo.h>
}

class SynthGUI : public QFrame
{
    Q_OBJECT

public:
    SynthGUI(const char * host, const char * port,
	     QByteArray controlPath, QByteArray midiPath, QByteArray programPath,
	     QByteArray exitingPath, QWidget *w = 0);
    virtual ~SynthGUI();

    bool ready() const { return m_ready; }
    void setReady(bool ready) { m_ready = ready; }

    void setHostRequestedQuit(bool r) { m_hostRequestedQuit = r; }

public slots:
    void setTuning (float hz);
    void setAttack (float sec);
    void setDecay  (float sec);
    void setSustain(float percent);
    void setRelease(float sec);
    void setTimbre (float val);
    void aboutToQuit();

protected slots:
    void tuningChanged (int);
    void attackChanged (int);
    void decayChanged  (int);
    void sustainChanged(int);
    void releaseChanged(int);
    void timbreChanged (int);
    void test_press();
    void test_release();
    void oscRecv();

protected:
    QDial *newQDial( int, int, int, int );
    
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
    QByteArray m_controlPath;
    QByteArray m_midiPath;
    QByteArray m_programPath;
    QByteArray m_exitingPath;

    bool m_suppressHostUpdate;
    bool m_hostRequestedQuit;
    bool m_ready;
};


#endif
