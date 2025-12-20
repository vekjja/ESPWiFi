import React from "react";
import {
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  Box,
  useTheme,
  useMediaQuery,
} from "@mui/material";

export default function SettingsModal({
  open,
  onClose,
  title,
  children,
  actions,
  maxWidth = "sm",
  fullWidth = true,
  disableEscapeKeyDown = false,
}) {
  const theme = useTheme();
  const isMobile = useMediaQuery(theme.breakpoints.down("sm"));

  return (
    <Dialog
      open={open}
      onClose={onClose}
      maxWidth={maxWidth}
      fullWidth={fullWidth}
      fullScreen={isMobile}
      disableEscapeKeyDown={disableEscapeKeyDown}
      PaperProps={{
        sx: {
          bgcolor: "background.paper",
          margin: isMobile ? 0 : "auto",
          width: isMobile ? "90%" : maxWidth === false ? "650px" : "auto",
          height: isMobile ? "auto" : "auto",
          minWidth: isMobile ? "90%" : maxWidth === false ? "650px" : "400px",
          maxWidth: isMobile ? "90%" : maxWidth === false ? "650px" : "600px",
          maxHeight: isMobile ? "90%" : "80vh",
          borderRadius: theme.shape.borderRadius,
          display: "flex",
          flexDirection: "column",
        },
      }}
      sx={{
        "& .MuiDialog-paper": {
          margin: isMobile ? 0 : "auto",
        },
        "& .MuiBackdrop-root": {
          backdropFilter: "blur(4px)",
          backgroundColor: "rgba(0, 0, 0, 0.5)",
        },
      }}
    >
      <DialogTitle
        sx={{
          pb: isMobile ? 2 : 1.5,
          pt: isMobile ? 3 : 2,
          px: isMobile ? 3 : 2,
          fontSize: isMobile ? "1.25rem" : "1.125rem",
          fontWeight: 600,
          color: "text.primary",
        }}
      >
        {title}
      </DialogTitle>
      <DialogContent
        sx={{
          flex: 1,
          p: isMobile ? 3 : 2,
          pt: isMobile ? 2 : 1.5,
          pb: isMobile ? 2 : 1.5,
          overflowY: "auto",
          "&::-webkit-scrollbar": {
            width: "6px",
          },
          "&::-webkit-scrollbar-track": {
            background: "rgba(255, 255, 255, 0.1)",
            borderRadius: "3px",
          },
          "&::-webkit-scrollbar-thumb": {
            background: "rgba(255, 255, 255, 0.3)",
            borderRadius: "3px",
            "&:hover": {
              background: "rgba(255, 255, 255, 0.5)",
            },
          },
        }}
      >
        <Box sx={{ mt: 0 }}>{children}</Box>
      </DialogContent>
      {actions && (
        <DialogActions
          sx={{
            p: isMobile ? 3 : 2,
            pt: isMobile ? 2 : 1.5,
            gap: isMobile ? 2 : 1,
            flexWrap: "wrap",
            justifyContent: "flex-end",
            "& .MuiButton-root": {
              minWidth: isMobile ? "120px" : "100px",
              height: isMobile ? "48px" : "36px",
              fontSize: isMobile ? "0.875rem" : "0.8125rem",
            },
          }}
        >
          {actions}
        </DialogActions>
      )}
    </Dialog>
  );
}
