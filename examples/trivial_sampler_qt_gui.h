/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* trivial_sampler_qt_gui.h

   Disposable Hosted Soft Synth API
   Constructed by Chris Cannam, Steve Harris and Sean Bolton

   A straightforward DSSI plugin sampler Qt GUI.

   This example file is in the public domain.
*/

#ifndef _TRIVIAL_SAMPLER_QT_GUI_H_INCLUDED_
#define _TRIVIAL_SAMPLER_QT_GUI_H_INCLUDED_

#include <qframe.h>
#include <qcheckbox.h>
#include <qspinbox.h>
#include <qlabel.h>
#include <qlayout.h>

extern "C" {
#include <lo/lo.h>
}

class SamplerGUI : public QFrame
{
    Q_OBJECT

public:
    SamplerGUI(QString host, QString port,
	       QString controlPath, QString midiPath, QString programPath,
	       QString exitingPath, QWidget *w = 0);
    virtual ~SamplerGUI();

    bool ready() const { return m_ready; }
    void setReady(bool ready) { m_ready = ready; }

    void setHostRequestedQuit(bool r) { m_hostRequestedQuit = r; }

public slots:
    void setBasePitch(int pitch);
    void setSustain(bool sustain);
    void aboutToQuit();

protected slots:
    void fileSelect();
    void basePitchChanged(int);
    void sustainChanged(bool);
    void test_press();
    void test_release();
    void oscRecv();

protected:
    QLabel *m_sampleFile;
    QSpinBox *m_basePitch;
    QCheckBox *m_sustain;

    lo_address m_host;
    QString m_controlPath;
    QString m_midiPath;
    QString m_configurePath;
    QString m_exitingPath;

    bool m_suppressHostUpdate;
    bool m_hostRequestedQuit;
    bool m_ready;
};


#endif
