use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Color, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph, Wrap};
use ratatui::Frame;

use crate::terminal::{Terminal, COLS, ROWS};

pub struct StatusInfo {
    pub device_status: u8,
    pub buf: u8,
    pub connected: bool,
    pub verbose: bool,
}

pub fn draw(
    frame: &mut Frame,
    terminal: &Terminal,
    status: &StatusInfo,
    log: &[String],
) {
    let outer = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Min(ROWS as u16 + 2), // terminal + border
            Constraint::Length(12),            // log pane
        ])
        .split(frame.area());

    let top = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Length(COLS as u16 + 2), // terminal + border
            Constraint::Min(20),                 // status
        ])
        .split(outer[0]);

    draw_terminal(frame, terminal, top[0]);
    draw_status(frame, status, top[1]);
    draw_log(frame, log, outer[1]);
}

fn draw_terminal(frame: &mut Frame, terminal: &Terminal, area: Rect) {
    let mut lines = Vec::with_capacity(ROWS);
    for (row_idx, row) in terminal.cells.iter().enumerate() {
        let mut spans = Vec::new();
        for (col_idx, cell) in row.iter().enumerate() {
            let mut style = cell.style;
            // Show cursor as reversed
            if row_idx == terminal.cursor_row && col_idx == terminal.cursor_col {
                style = style.fg(Color::Black).bg(Color::White);
            }
            spans.push(Span::styled(String::from(cell.ch), style));
        }
        lines.push(Line::from(spans));
    }

    let block = Block::default()
        .title(" Terminal ")
        .borders(Borders::ALL);
    let paragraph = Paragraph::new(lines).block(block);
    frame.render_widget(paragraph, area);
}

fn draw_status(frame: &mut Frame, status: &StatusInfo, area: Rect) {
    let mut lines = vec![
        Line::from(if status.connected { "Connected" } else { "Disconnected" }),
        Line::from(format!("BUF: {}", status.buf)),
        Line::from(format!("Verbose: {}", if status.verbose { "ON" } else { "off" })),
        Line::from(""),
        Line::from("Devices:"),
    ];

    let device_names = ["Status", "System", "Video/KB", "Netboot", "Network", "Free", "Free", "Echo"];
    for (i, name) in device_names.iter().enumerate() {
        let active = status.device_status & (1 << i) != 0;
        let marker = if active { ">" } else { " " };
        let style = if active {
            Style::default().fg(Color::Green)
        } else {
            Style::default().fg(Color::DarkGray)
        };
        lines.push(Line::styled(format!(" {marker} {i}: {name}"), style));
    }

    lines.push(Line::from(""));
    lines.push(Line::styled("F1 verbose | Ctrl-C quit", Style::default().fg(Color::DarkGray)));

    let block = Block::default()
        .title(" Status ")
        .borders(Borders::ALL);
    let paragraph = Paragraph::new(lines).block(block);
    frame.render_widget(paragraph, area);
}

fn draw_log(frame: &mut Frame, log: &[String], area: Rect) {
    let inner_height = area.height.saturating_sub(2) as usize; // minus border
    let skip = log.len().saturating_sub(inner_height);
    let lines: Vec<Line> = log[skip..]
        .iter()
        .map(|s| Line::from(s.as_str()))
        .collect();

    let block = Block::default()
        .title(" Log ")
        .borders(Borders::ALL);
    let paragraph = Paragraph::new(lines).block(block).wrap(Wrap { trim: false });
    frame.render_widget(paragraph, area);
}
