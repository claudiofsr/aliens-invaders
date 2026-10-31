#include <algorithm>
#include <array>
#include <cstddef>

#include "stage_fanfare.h"

namespace StageFanfare {

// Composer and work metadata for the fifteen wave arrangements. The strings
// feed the jukebox CLI (-tracks, -play) and the in-game soundtrack pages.
const std::array<PieceMetadata, kPieceCount>& AllMetadata() noexcept {
  static const std::array<PieceMetadata, kPieceCount> table = {{
      {"Wolfgang Amadeus Mozart", "Austrian", "1756-1791",
       "Rondo alla Turca (Turkish March)", "K. 331", "III. Alla turca - Allegretto (The Janissary March)",
       "Mozart never called it the Turkish March: the rondo imitates the janissary bands of the Ottoman siege of Vienna - bass drum, cymbals and piccolo - turning a once-feared enemy into the city's favorite dance craze."},

      {"Carlos Gomes", "Brazilian", "1836-1896",
       "O Guarani", "Il Guarany Overture", "Complete Overture - Andante (A Voz do Brasil) through the Allegro brillante cadence (mm. 1-65)",
       "Brazil's greatest operatic export; the overture was once so popular in Italy that street organs played it daily."},

      {"Giacomo Puccini", "Italian", "1858-1924",
       "Turandot", "SC 91", "Nessun Dorma (Vincero!)",
       "At the 1926 premiere Toscanini stopped the orchestra exactly where Puccini's ink had run dry - the rest was silence."},

      {"Pyotr Ilyich Tchaikovsky", "Russian", "1840-1893",
       "Swan Lake", "Op. 20", "Danse des petits cygnes (Dance of the Little Swans)",
       "First written as a private puppet ballet for his nieces; the public premiere was a disaster, yet the melody survived."},

      {"Ludwig van Beethoven", "German", "1770-1827",
       "Symphony No. 5 in C minor", "Op. 67", "I. Allegro con brio - The Fate Motif",
       "'Fate knocking at the door': in WWII the BBC opened its broadcasts with these four notes, whose rhythm is the Morse code for V - Victory."},

      {"Johann Sebastian Bach", "German", "1685-1750",
       "Toccata and Fugue in D minor", "BWV 565", "Toccata - The Opening Thunderbolt",
       "The most famous organ work ever written opens with a single mordented A and a cascading diminished seventh - a storm that cinema later adopted as the sound of menace itself."},

      {"Pyotr Ilyich Tchaikovsky", "Russian", "1840-1893",
       "The Nutcracker", "Op. 71", "Waltz of the Flowers (Valse des fleurs)",
       "The suite's grand finale opens with a cascading harp cadenza; the flowers of the Sugar Plum Fairy's court then dance what became the most celebrated waltz in ballet history."},

      {"Maurice Ravel", "French", "1875-1937",
       "Bolero", "M. 81", "Tempo di Bolero, moderato assai",
       "Ravel himself joked that the piece contained 'no music, only orchestration' - a 15-minute crescendo of colour."},

      {"Gioachino Rossini", "Italian", "1792-1868",
       "The Barber of Seville", "Overture", "Allegro vivace - The Rossini Crescendo",
       "Written in under three weeks under producer pressure; the overture still sounds like pure champagne."},

      {"Edvard Grieg", "Norwegian", "1843-1907",
       "Peer Gynt Suite No. 1", "Op. 46", "Morning Mood (Morgenstemning)",
       "Although the music paints Norwegian dawn, Ibsen's scene is set in the Moroccan desert at sunrise."},

      {"Zequinha de Abreu", "Brazilian", "1880-1935",
       "Tico-Tico no Fuba", "Choro", "O tico-tico ta comendo meu fuba (Allegro Vivace)",
       "A sparrow stealing cornmeal became the unofficial anthem of Brazilian choro - joyful, syncopated, unstoppable."},

      {"Richard Wagner", "German", "1813-1883",
       "Die Walkure (The Valkyrie)", "WWV 86B", "Act III Prelude - Ride of the Valkyries",
       "Wagner scored the war-maidens' gallop over a storm of tremolo strings, its leaping brass call blazing through the whole orchestra; a century of cinema - from Apocalypse Now onward - made it the most famous battle music ever written."},

      {"Frederic Chopin", "Polish", "1810-1849",
       "Fantaisie-Impromptu in C-sharp minor", "Op. 66", "Allegro agitato - The Cascading Opening",
       "Chopin forbade its publication, yet Fontana printed it after his death; four right-hand notes weave against every three in the left, and the singing middle theme later became the song I'm Always Chasing Rainbows."},

      {"Claude Debussy", "French", "1862-1918",
       "Suite Bergamasque", "L. 75", "Clair de Lune",
       "Born from Verlaine's poem of souls drifting under a sad, beautiful moon - impressionism in pure sound."},

      {"Antonio Vivaldi", "Italian", "1678-1741",
       "The Four Seasons: Spring", "RV 269", "I. Allegro (Giunt' e la primavera)",
       "The 'Red Priest' published sonnets with the score so listeners would hear the birds, storms and dancing shepherds."}
  }};
  return table;
}

const PieceMetadata& GetMetadata(int wave) noexcept {
  const auto& table = AllMetadata();
  const size_t idx = static_cast<size_t>(std::clamp(wave - 1, 0, kPieceCount - 1));
  return table[idx];
}

}  // namespace StageFanfare
