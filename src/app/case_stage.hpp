#pragma once

#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include <array>
#include <cstddef>
#include <vector>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QSplitter;

namespace vcube {

class AlgGuide;
class CubeView;

// Where Learn shows a case: the virtual cube above, with a header and a
// row of options, and below it what is said about the case: the guide,
// the status, the grade, the quiz's answers, the algs and the case's
// family. The cube and what is below it share the height as the splitter
// between them says.
class CaseStage : public QWidget {
  Q_OBJECT
public:
  explicit CaseStage(QWidget *parent = nullptr);

  QLabel *header;      // stays in focus mode
  QWidget *optionsRow; // the page's options, hidden in focus mode
  QHBoxLayout *optionsLayout;
  CubeView *cube;
  QLabel *status;
  QLabel *reveal;    // the algs
  QWidget *gradeRow; // the outcome, and the grade buttons
  std::array<QPushButton *, 4> gradeButtons{}; // Again, Hard, Good, Easy
  QSplitter *splitter;                         // between the cube and the rest

  // The guide: every move of the alg, the next one marked (a walkthrough,
  // or following each next move), or with `nextOnly` the next move alone
  // (a learning-step hint). While `solving`, a cube between two moves or
  // off the alg (`deviated`) is said so. Null hides it.
  void showGuide(const AlgGuide *guide, bool nextOnly, bool solving,
                 bool deviated);
  // The outcome: a coloured badge and the words beside it.
  void showOutcome(const QString &badge, const QString &colour,
                   const QString &detail);

  // The quiz's answers, numbered from 1, or none; quizAnswered() tells
  // which was clicked.
  void setQuizChoices(const QStringList &names);
  // The right answer is marked, and a wrong `chosen` one; the others are
  // turned off.
  void showQuizAnswer(std::size_t right, std::size_t chosen);
  void hideQuiz();

  // After a case learned by family: every case of the family.
  struct FamilyCase {
    QPixmap picture;
    QString name;
    QString alg; // as rich text
    bool done;   // the case just done (else a mirror image of it)
  };
  void showFamily(const std::vector<FamilyCase> &cases);
  void hideFamily();

signals:
  void quizAnswered(std::size_t choice);

private:
  QLabel *guide_;
  QLabel *gradeBadge_;
  QLabel *gradeLabel_;
  QWidget *quizRow_;
  QWidget *familyRow_;
};

} // namespace vcube
