/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* less_trivial_synth_qt_gui.cpp

   Disposable Hosted Soft Synth API version 0.1
   Constructed by Chris Cannam and Steve Harris

   This is an example Qt GUI for an example DSSI synth plugin.

   This example file is in the public domain.
*/

#include "less_trivial_synth_qt_gui.h"

#include <qapplication.h>

SynthGUI::SynthGUI(QWidget *w) :
    QFrame(w)
{
    QGridLayout *layout = new QGridLayout(this, 3, 5, 5, 5);
    
    m_tuning  = new QDial(100, 600, 10, 400, this); // (Hz - 400) * 10
    m_attack  = new QDial(  1, 100,  1,  25, this); // s * 100
    m_decay   = new QDial(  1, 100,  1,  25, this); // s * 100
    m_sustain = new QDial(  0, 100,  1,  75, this); // %
    m_release = new QDial(  1, 400, 10, 200, this); // s * 100
    
    m_tuning ->setNotchesVisible(true);
    m_attack ->setNotchesVisible(true);
    m_decay  ->setNotchesVisible(true);
    m_sustain->setNotchesVisible(true);
    m_release->setNotchesVisible(true);

    m_tuningLabel  = new QLabel(this);
    m_attackLabel  = new QLabel(this);
    m_decayLabel   = new QLabel(this);
    m_sustainLabel = new QLabel(this);
    m_releaseLabel = new QLabel(this);
    
    layout->addWidget(new QLabel("Tuning A",      this), 0, 0, Qt::AlignCenter);
    layout->addWidget(new QLabel("Attack time",   this), 0, 1, Qt::AlignCenter);
    layout->addWidget(new QLabel("Decay time",    this), 0, 2, Qt::AlignCenter);
    layout->addWidget(new QLabel("Sustain level", this), 0, 3, Qt::AlignCenter);
    layout->addWidget(new QLabel("Release time",  this), 0, 4, Qt::AlignCenter);
    
    layout->addWidget(m_tuning,  1, 0);
    layout->addWidget(m_attack,  1, 1);
    layout->addWidget(m_decay,   1, 2);
    layout->addWidget(m_sustain, 1, 3);
    layout->addWidget(m_release, 1, 4);
    
    layout->addWidget(m_tuningLabel,  2, 0, Qt::AlignCenter);
    layout->addWidget(m_attackLabel,  2, 1, Qt::AlignCenter);
    layout->addWidget(m_decayLabel,   2, 2, Qt::AlignCenter);
    layout->addWidget(m_sustainLabel, 2, 3, Qt::AlignCenter);
    layout->addWidget(m_releaseLabel, 2, 4, Qt::AlignCenter);

    connect(m_tuning,  SIGNAL(valueChanged(int)), this, SLOT(tuningChanged(int)));
    connect(m_attack,  SIGNAL(valueChanged(int)), this, SLOT(attackChanged(int)));
    connect(m_decay,   SIGNAL(valueChanged(int)), this, SLOT(decayChanged(int)));
    connect(m_sustain, SIGNAL(valueChanged(int)), this, SLOT(sustainChanged(int)));
    connect(m_release, SIGNAL(valueChanged(int)), this, SLOT(releaseChanged(int)));

    // cause some initial updates
    tuningChanged (m_tuning ->value());
    attackChanged (m_attack ->value());
    decayChanged  (m_decay  ->value());
    sustainChanged(m_sustain->value());
    releaseChanged(m_release->value());
}

void
SynthGUI::tuningChanged(int value)
{
    float hz = float(value) / 10.0 + 400.0;
    m_tuningLabel->setText(QString("%1 Hz").arg(hz));
    
    //!!! send hz to host
}

void
SynthGUI::attackChanged(int value)
{
    float sec = float(value) / 100.0;
    m_attackLabel->setText(QString("%1 sec").arg(sec));

    //!!! send sec to host
}

void
SynthGUI::decayChanged(int value)
{
    float sec = float(value) / 100.0;
    m_decayLabel->setText(QString("%1 sec").arg(sec));

    //!!! send sec to host
}

void
SynthGUI::sustainChanged(int value)
{
    m_sustainLabel->setText(QString("%1 %").arg(value));

    //!!! send value (percent) to host
}

void
SynthGUI::releaseChanged(int value)
{
    float sec = float(value) / 100.0;
    m_releaseLabel->setText(QString("%1 sec").arg(sec));

    //!!! send sec to host
}

SynthGUI::~SynthGUI()
{
}

int
main(int argc, char **argv)
{
    QApplication application(argc, argv);

    SynthGUI gui;
    application.setMainWidget(&gui);
    gui.show();

    //!!! now... updates from the host?

    return application.exec();
}

