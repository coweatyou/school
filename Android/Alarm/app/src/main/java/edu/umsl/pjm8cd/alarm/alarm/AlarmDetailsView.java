package edu.umsl.pjm8cd.alarm;

import android.app.Activity;
import android.app.Dialog;
import android.app.TimePickerDialog;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.provider.MediaStore;
import android.support.annotation.Nullable;
import android.support.v4.app.DialogFragment;
import android.support.v4.app.Fragment;
import android.text.InputType;
import android.text.format.DateFormat;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.TimePicker;
import android.widget.Toast;

import java.util.Calendar;
import java.util.Date;
import java.util.UUID;

import edu.umsl.pjm8cd.alarm.database.DBWrapper;

/**
 * Created by Pat on 5/1/2016.
 *
 *
 */
public class AlarmDetailsView extends Fragment {
    private EditText alarmName;
    private TextView alarmTime;
    private Switch active; //TODO this should dispatch every time it is checked and undispatch every time it is unchecked, also needs to be added to the db.
    private DBWrapper database;

    @Nullable
    @Override
    public View onCreateView(LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        setRetainInstance(true);
        View view = inflater.inflate(R.layout.edit_alarm, container, false);

        database = new DBWrapper(getContext());

        alarmName = (EditText) view.findViewById(R.id.alarm_name);
        alarmTime = (TextView) view.findViewById(R.id.alarm_time);
        active = (Switch) view.findViewById(R.id.alarm_enabled);

        String specifiedUUID = getActivity().getIntent().getStringExtra(AlarmDetailActivity.UUID);
        if(specifiedUUID != null) { // Is user viewing an existing contact or creating a new one?
            Alarm c = DBWrapper.get(getActivity()).getAlarmFromUUID(UUID.fromString(specifiedUUID));
            fillFields(c);
        } else {
//            inEditMode = true;
//            setEditable(true);
        }

        ((Button) view.findViewById(R.id.change_time_button)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                showTimePickerDialog(view);
            }
        });

        ((Button) view.findViewById(R.id.save_button)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                //TODO
                // Add contact with the given info into the database.
                if (alarmName.getText().toString().isEmpty() || alarmTime.getText().toString().isEmpty()) {
                    Toast.makeText(getContext(), "Alarm must have a name and a time.", Toast.LENGTH_SHORT).show();
                } else {
                    Alarm a = buildContact();
                    database.updateOrAddAlarm(a);
                    getActivity().finish();
                }
            }
        });

        ((Button) view.findViewById(R.id.cancel_button)).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                getActivity().finish();
            }
        });

        return view;
    }

    public void showTimePickerDialog(View v) {
        DialogFragment newFragment = new TimePickerFragment();
        ((TimePickerFragment) newFragment).setAlarmTime(alarmTime);
        newFragment.show(getActivity().getSupportFragmentManager(), "timePicker");
    }

    /* Fills the text areas in the view with the given contact info. */
    private void fillFields(Alarm c) {
        alarmName.setText(c.getName());
        active.setChecked(c.isRunning());
        alarmTime.setText(c.getTime());
    }

    /* Takes the view fields and builds a Alarm. */
    public Alarm buildContact() {
        String name = alarmName.getText().toString();
        String[] time = alarmTime.getText().toString().split(":");

        int hour = Integer.parseInt(time[0]);
        int min = Integer.parseInt(time[1]);
//        int mid = time.lastIndexOf(":");
//        int hour = Integer.parseInt(time.substring(0,1));
//        int min = Integer.parseInt(time.substring(2,3));

        /*Calendar cur = Calendar.getInstance();
        Calendar future = Calendar.getInstance();
        future.set(Calendar.HOUR, hour);
        future.set(Calendar.MINUTE, min);

        if(cur.before(future)) { // If the new time is on the same day as today.

        }

        int curHours = Calendar.getInstance().get(Calendar.HOUR);
        int curMin = Calendar.getInstance().get(Calendar.MINUTE);*/

        Alarm newTimer;
        // If we are updating an existing contact, use the same UUID
        String contactUUID = getActivity().getIntent().getStringExtra(AlarmDetailActivity.UUID);
        if(contactUUID == null) {
            newTimer = new Alarm();
        } else {
            newTimer = new Alarm(UUID.fromString(contactUUID));
        }

        newTimer.setName(name);
        newTimer.setHours(hour);
        newTimer.setMinutes(min);
        newTimer.setRunning(active.isChecked());
        return newTimer;
    }

    public static class TimePickerFragment extends DialogFragment
            implements TimePickerDialog.OnTimeSetListener {
        private TextView alarmTime;

        @Override
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            // Use the current time as the default values for the picker
            final Calendar c = Calendar.getInstance();
            int hour = c.get(Calendar.HOUR_OF_DAY);
            int minute = c.get(Calendar.MINUTE);

            // Create a new instance of TimePickerDialog and return it
            return new TimePickerDialog(getActivity(), this, hour, minute,
                    DateFormat.is24HourFormat(getActivity()));
        }

        public void onTimeSet(TimePicker view, int hourOfDay, int minute) {
            alarmTime.setText(hourOfDay + ":" + minute);
        }
        public void setAlarmTime(TextView alarmTime) {
            this.alarmTime = alarmTime;
        }
    }
//
//    /* Set to the given photo or to a default image. */
//    private void updatePictureView() {
//        if(picture == null) {
//            pictureView.setBackgroundResource(R.drawable.ben);
//        } else {
//            pictureView.setBackgroundResource(android.R.color.white);
//            pictureView.setImageBitmap(picture);
//        }
//    }
//
//    @Override
//    public void onActivityResult(int requestCode, int resultCode, Intent data) {
//        if(resultCode == Activity.RESULT_OK) { // Returned from the camera.
//            picture = (Bitmap) data.getExtras().get("data");
//            updatePictureView();
//        }
//    }
//
//    /* Fills the text and photo areas in the view with the given contact info. */
//    private void fillFields(Alarm c) {
//        alarmName.setText(c.getFirstName());
//        alarmTime.setText(c.getLastName());
//        email.setText(c.getEmail());
//        picture = c.getPicture();
//        updatePictureView();
//    }
//
//    /* Allows the user to edit the data. */
//    public void setEditable(boolean editable) {
//        if(editable) {
//            alarmName.setInputType(InputType.TYPE_CLASS_TEXT);
//            alarmTime.setInputType(InputType.TYPE_CLASS_TEXT);
//            email.setInputType(InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS | InputType.TYPE_TEXT_VARIATION_WEB_EMAIL_ADDRESS);
//            camButton.setVisibility(View.VISIBLE);
//        } else {
//            alarmName.setInputType(InputType.TYPE_NULL);
//            alarmTime.setInputType(InputType.TYPE_NULL);
//            email.setInputType(InputType.TYPE_NULL);
//            camButton.setVisibility(View.GONE);
//        }
//        inEditMode = editable;
//    }
//
//    /* Takes the view fields and builds a Alarm. */
//    public Alarm buildContact() {
//        String first = alarmName.getText().toString();
//        String last = alarmTime.getText().toString();
//        String addr = email.getText().toString();
//
//        Alarm newTimer;
//        // If we are updating an existing contact, use the same UUID
//        String contactUUID = getActivity().getIntent().getStringExtra(AlarmDetailActivity.UUID);
//        if(contactUUID == null) {
//            newTimer = new Alarm();
//        } else {
//            newTimer = new Alarm(UUID.fromString(contactUUID));
//        }
//
//        newTimer.setFirstName(first);
//        newTimer.setLastName(last);
//        newTimer.setEmail(addr);
//        newTimer.setPicture(picture);
//        return newTimer;
//    }
//
//    public boolean isInEditMode() {
//        return inEditMode;
//    }
}