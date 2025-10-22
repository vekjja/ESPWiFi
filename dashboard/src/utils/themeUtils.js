import { useTheme } from "@mui/material/styles";

/**
 * Hook to access custom theme properties
 * @returns {Object} Custom theme properties
 */
export const useCustomTheme = () => {
  const theme = useTheme();
  return theme.custom || {};
};

/**
 * Get the save icon from theme
 * @param {Object} theme - MUI theme object
 * @returns {React.Component} Save icon component
 */
export const getSaveIcon = (theme) => {
  return theme.custom?.icons?.save;
};

/**
 * Get the delete icon from theme
 * @param {Object} theme - MUI theme object
 * @returns {React.Component} Delete icon component
 */
export const getDeleteIcon = (theme) => {
  return theme.custom?.icons?.delete;
};

/**
 * Get the edit icon from theme
 * @param {Object} theme - MUI theme object
 * @returns {React.Component} Edit icon component
 */
export const getEditIcon = (theme) => {
  return theme.custom?.icons?.edit;
};

/**
 * Get all icons from theme
 * @param {Object} theme - MUI theme object
 * @returns {Object} Icons object
 */
export const getThemeIcons = (theme) => {
  return theme.custom?.icons || {};
};
