package utils;

import allnetui.SocketUtils;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.Toolkit;
import java.awt.datatransfer.Clipboard;
import java.awt.datatransfer.StringSelection;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import javax.swing.BoxLayout;
import javax.swing.JComponent;
import javax.swing.JMenuItem;
import javax.swing.JPanel;
import javax.swing.JPopupMenu;
import javax.swing.JTextPane;

/**
 * Class to create a "message bubble" as a JTextPane.
 *
 * A border can be added externally.
 *
 * @author henry
 * @param <MESSAGE>
 */
public class MessageBubble<MESSAGE> extends JPanel implements ActionListener {

    private static final String COPY = "Copy";
    private static final String COPY_ALL = "Copy All";

    // width of the container the last time this MessageBubble was resized
    private int lastContainerWidth;

    // keep the message pane so we can change background later
    private JTextPane textPane;
    // keep ref to popup since text panes will need to reference it
    private JPopupMenu popup;
    // keep a reference to the message that the Bubble renders, if desired
    private MESSAGE message;
    //
    // needed for when we resize the bubble
    private String text;
    private boolean leftJustified;
    //
    // utility for word wrapping and selection correction
    private WordWrapper ww = new WordWrapper(true);

    
    public MessageBubble(MESSAGE message, boolean leftJustified, Color color,
        String text, JComponent container) {
        super();
        this.message = message;
        this.leftJustified = leftJustified;
        this.text = text;
        setBackground(color);
        lastContainerWidth = container.getWidth();
        // int charsPerLine = findCharsPerLine(lastContainerWidth);
        textPane = makeTextPane(color, leftJustified, lastContainerWidth);
        setLayout(new BoxLayout(this, BoxLayout.X_AXIS));
        add(textPane);
        // make a context menu
        popup = new JPopupMenu();
        JMenuItem item = new JMenuItem(COPY);
        item.addActionListener(this);
        popup.add(item);
        item = new JMenuItem(COPY_ALL);
        item.addActionListener(this);
        popup.add(item);
        textPane.setComponentPopupMenu(popup);
    }

    private JTextPane makeTextPane(Color color, boolean leftJustified,
        int containerWidth) {
        JTextPane pane = null;
        int width = containerWidth;
        do {
            pane = makeTextPaneQuick(color, leftJustified, width);
            width = (9 * width) / 10;
        }
        while (pane.getPreferredSize().width > (2 * containerWidth) / 3);
        return (pane);
    }

    public void resizeBubble(int width) {
        remove(textPane);
        textPane = makeTextPane(textPane.getBackground(),
            leftJustified, width);
        textPane.setComponentPopupMenu(popup);
        add(textPane);
    }

    private JTextPane makeTextPaneQuick(Color color, boolean leftJustified,
        int containerWidth) {
        JTextPane pane = new JTextPane();
        pane.setContentType("text/html");
        pane.setEditable(false);
        pane.setBackground(color);
        // 5 pix per char is really small
        ww.wordWrapText(text, containerWidth / 5, !leftJustified);
        String[] wordWrappedLines = ww.getWrappedText();
        String htmlPrefix;
        if (leftJustified) {
            htmlPrefix = "<STYLE type=\"text/css\"> BODY {text-align: left} </STYLE> <BODY>";
        }
        else {
            htmlPrefix = "<STYLE type=\"text/css\"> BODY {text-align: right} </STYLE> <BODY>";
        }
        StringBuilder sb = new StringBuilder(htmlPrefix);
        for (int i = 0; i < wordWrappedLines.length; i++) {
            sb.append(nbspMe(wordWrappedLines[i]));
            if (i < wordWrappedLines.length - 1) {
                sb.append("<br>");
            }
        }
        sb.append("</BODY>");
        // use &nbsp; here as <pre> does not seem to work
        pane.setText(sb.toString());
        // without this, panel grows to fill scrollpane
        Dimension size = pane.getPreferredScrollableViewportSize();
        pane.setPreferredSize(size);
        pane.setMaximumSize(size);
        return (pane);
    }

    private String nbspMe(String s) {
        String r = s.replaceAll(" ", "&nbsp;");
        return (r);
    }
    
    public void setBubbleBackground(Color bg) {
        super.setBackground(bg);
        textPane.setBackground(bg);
    }

    public MESSAGE getMessage() {
        return (message);
    }

    @Override
    public void actionPerformed(ActionEvent e) {
        String selected, corrected;
        int startIdx;
        String cmd = e.getActionCommand();
        switch (cmd) {
            case COPY_ALL:
                corrected = text;
                break;
            case COPY:
                selected = textPane.getSelectedText();
                // offset of 1 determined experimentally, apparently undocumented
                startIdx = textPane.getSelectionStart() - 1;
                corrected = ww.getCorrected(selected, startIdx);
                break;
            default:
                throw new RuntimeException("bad menu cmd");
        }
        StringSelection selection = new StringSelection(corrected);
        Clipboard clipboard = Toolkit.getDefaultToolkit().getSystemClipboard();
        clipboard.setContents(selection, selection);
    }

}
