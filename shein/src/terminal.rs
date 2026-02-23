use ratatui::style::{Color, Modifier, Style};
use vte::{Params, Perform, Parser};

pub const COLS: usize = 40;
pub const ROWS: usize = 25;

#[derive(Clone, Copy)]
pub struct Cell {
    pub ch: char,
    pub style: Style,
}

impl Default for Cell {
    fn default() -> Self {
        Self {
            ch: ' ',
            style: Style::default(),
        }
    }
}

pub struct Terminal {
    pub cells: [[Cell; COLS]; ROWS],
    pub cursor_row: usize,
    pub cursor_col: usize,
    current_style: Style,
    parser: Parser,
}

impl Terminal {
    pub fn new() -> Self {
        Self {
            cells: [[Cell::default(); COLS]; ROWS],
            cursor_row: 0,
            cursor_col: 0,
            current_style: Style::default(),
            parser: Parser::new(),
        }
    }

    /// Feed raw bytes from device 2 into the terminal.
    pub fn feed(&mut self, bytes: &[u8]) {
        for &b in bytes {
            // VTE parser calls back into our Perform impl via a helper closure.
            // We need to use a temporary because Parser::advance takes &mut self
            // and calls Perform methods on a separate receiver.
            let mut parser = std::mem::replace(&mut self.parser, Parser::new());
            parser.advance(self, b);
            self.parser = parser;
        }
    }

    fn scroll_up(&mut self) {
        for row in 1..ROWS {
            self.cells[row - 1] = self.cells[row];
        }
        self.cells[ROWS - 1] = [Cell::default(); COLS];
    }

    fn advance_cursor(&mut self) {
        self.cursor_col += 1;
        if self.cursor_col >= COLS {
            self.cursor_col = 0;
            self.cursor_row += 1;
            if self.cursor_row >= ROWS {
                self.scroll_up();
                self.cursor_row = ROWS - 1;
            }
        }
    }

    fn newline(&mut self) {
        self.cursor_col = 0;
        self.cursor_row += 1;
        if self.cursor_row >= ROWS {
            self.scroll_up();
            self.cursor_row = ROWS - 1;
        }
    }

    fn apply_sgr(&mut self, params: &Params) {
        // Collect into a flat Vec so we can index-advance for extended colors
        let p: Vec<u16> = params.iter().map(|s| s[0]).collect();
        let mut i = 0;
        while i < p.len() {
            match p[i] {
                0 => self.current_style = Style::default(),
                1 => self.current_style = self.current_style.add_modifier(Modifier::BOLD),
                3 => self.current_style = self.current_style.add_modifier(Modifier::ITALIC),
                4 => self.current_style = self.current_style.add_modifier(Modifier::UNDERLINED),
                7 => self.current_style = self.current_style.add_modifier(Modifier::REVERSED),
                9 => self.current_style = self.current_style.add_modifier(Modifier::CROSSED_OUT),
                22 => self.current_style = self.current_style.remove_modifier(Modifier::BOLD),
                23 => self.current_style = self.current_style.remove_modifier(Modifier::ITALIC),
                24 => self.current_style = self.current_style.remove_modifier(Modifier::UNDERLINED),
                27 => self.current_style = self.current_style.remove_modifier(Modifier::REVERSED),
                29 => self.current_style = self.current_style.remove_modifier(Modifier::CROSSED_OUT),
                // Foreground colors
                30 => self.current_style = self.current_style.fg(Color::Black),
                31 => self.current_style = self.current_style.fg(Color::Red),
                32 => self.current_style = self.current_style.fg(Color::Green),
                33 => self.current_style = self.current_style.fg(Color::Yellow),
                34 => self.current_style = self.current_style.fg(Color::Blue),
                35 => self.current_style = self.current_style.fg(Color::Magenta),
                36 => self.current_style = self.current_style.fg(Color::Cyan),
                37 => self.current_style = self.current_style.fg(Color::White),
                38 => {
                    if let Some((color, consumed)) = parse_extended_color(&p[i + 1..]) {
                        self.current_style = self.current_style.fg(color);
                        i += consumed;
                    }
                }
                39 => self.current_style = self.current_style.fg(Color::Reset),
                // Background colors
                40 => self.current_style = self.current_style.bg(Color::Black),
                41 => self.current_style = self.current_style.bg(Color::Red),
                42 => self.current_style = self.current_style.bg(Color::Green),
                43 => self.current_style = self.current_style.bg(Color::Yellow),
                44 => self.current_style = self.current_style.bg(Color::Blue),
                45 => self.current_style = self.current_style.bg(Color::Magenta),
                46 => self.current_style = self.current_style.bg(Color::Cyan),
                47 => self.current_style = self.current_style.bg(Color::White),
                48 => {
                    if let Some((color, consumed)) = parse_extended_color(&p[i + 1..]) {
                        self.current_style = self.current_style.bg(color);
                        i += consumed;
                    }
                }
                49 => self.current_style = self.current_style.bg(Color::Reset),
                // Bright foreground
                90..=97 => {
                    let bright = [
                        Color::DarkGray, Color::LightRed, Color::LightGreen, Color::LightYellow,
                        Color::LightBlue, Color::LightMagenta, Color::LightCyan, Color::White,
                    ];
                    self.current_style = self.current_style.fg(bright[(p[i] - 90) as usize]);
                }
                // Bright background
                100..=107 => {
                    let bright = [
                        Color::DarkGray, Color::LightRed, Color::LightGreen, Color::LightYellow,
                        Color::LightBlue, Color::LightMagenta, Color::LightCyan, Color::White,
                    ];
                    self.current_style = self.current_style.bg(bright[(p[i] - 100) as usize]);
                }
                _ => {}
            }
            i += 1;
        }
    }

    fn erase_in_display(&mut self, mode: u16) {
        match mode {
            0 => {
                // Erase from cursor to end
                for col in self.cursor_col..COLS {
                    self.cells[self.cursor_row][col] = Cell::default();
                }
                for row in (self.cursor_row + 1)..ROWS {
                    self.cells[row] = [Cell::default(); COLS];
                }
            }
            1 => {
                // Erase from start to cursor
                for row in 0..self.cursor_row {
                    self.cells[row] = [Cell::default(); COLS];
                }
                for col in 0..=self.cursor_col {
                    self.cells[self.cursor_row][col] = Cell::default();
                }
            }
            2 | 3 => {
                // Erase entire display
                self.cells = [[Cell::default(); COLS]; ROWS];
            }
            _ => {}
        }
    }

    fn erase_in_line(&mut self, mode: u16) {
        match mode {
            0 => {
                // Erase from cursor to end of line
                for col in self.cursor_col..COLS {
                    self.cells[self.cursor_row][col] = Cell::default();
                }
            }
            1 => {
                // Erase from start of line to cursor
                for col in 0..=self.cursor_col {
                    self.cells[self.cursor_row][col] = Cell::default();
                }
            }
            2 => {
                // Erase entire line
                self.cells[self.cursor_row] = [Cell::default(); COLS];
            }
            _ => {}
        }
    }
}

/// Parse extended color from remaining SGR params after a 38 or 48.
/// Returns (Color, number_of_params_consumed) or None.
fn parse_extended_color(rest: &[u16]) -> Option<(Color, usize)> {
    let kind = *rest.first()?;
    match kind {
        5 => {
            let idx = *rest.get(1)? as u8;
            Some((Color::Indexed(idx), 2))
        }
        2 => {
            let r = *rest.get(1)? as u8;
            let g = *rest.get(2)? as u8;
            let b = *rest.get(3)? as u8;
            Some((Color::Rgb(r, g, b), 4))
        }
        _ => None,
    }
}

impl Perform for Terminal {
    fn print(&mut self, c: char) {
        if self.cursor_row < ROWS && self.cursor_col < COLS {
            self.cells[self.cursor_row][self.cursor_col] = Cell {
                ch: c,
                style: self.current_style,
            };
        }
        self.advance_cursor();
    }

    fn execute(&mut self, byte: u8) {
        match byte {
            b'\n' => self.newline(),
            b'\r' => self.cursor_col = 0,
            0x08 => {
                // Backspace
                if self.cursor_col > 0 {
                    self.cursor_col -= 1;
                }
            }
            0x09 => {
                // Tab: advance to next 8-column boundary
                let target = (self.cursor_col + 8) & !7;
                self.cursor_col = target.min(COLS - 1);
            }
            _ => {}
        }
    }

    fn csi_dispatch(&mut self, params: &Params, _intermediates: &[u8], _ignore: bool, action: char) {
        let p: Vec<u16> = params.iter().map(|p| p[0]).collect();
        let p1 = || p.first().copied().unwrap_or(1).max(1) as usize;
        let p0 = || p.first().copied().unwrap_or(0);

        match action {
            'm' => self.apply_sgr(params),
            'A' => self.cursor_row = self.cursor_row.saturating_sub(p1()),
            'B' => self.cursor_row = (self.cursor_row + p1()).min(ROWS - 1),
            'C' => self.cursor_col = (self.cursor_col + p1()).min(COLS - 1),
            'D' => self.cursor_col = self.cursor_col.saturating_sub(p1()),
            'H' | 'f' => {
                // CUP: cursor position (1-based)
                let row = p.first().copied().unwrap_or(1).max(1) as usize - 1;
                let col = p.get(1).copied().unwrap_or(1).max(1) as usize - 1;
                self.cursor_row = row.min(ROWS - 1);
                self.cursor_col = col.min(COLS - 1);
            }
            'J' => self.erase_in_display(p0()),
            'K' => self.erase_in_line(p0()),
            'G' => {
                // CHA: cursor horizontal absolute (1-based)
                let col = p1() - 1;
                self.cursor_col = col.min(COLS - 1);
            }
            'd' => {
                // VPA: vertical position absolute (1-based)
                let row = p1() - 1;
                self.cursor_row = row.min(ROWS - 1);
            }
            _ => {}
        }
    }
}
